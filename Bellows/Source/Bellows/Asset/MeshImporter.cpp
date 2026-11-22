#include "bwpch.h"
#include "Bellows/Asset/MeshImporter.h"

#include "Bellows/Core/FileSystem.h"
#include "Bellows/Renderer/Texture.h"
#include "Bellows/Serialization/JsonUtils.h"

#include <cgltf.h>
#include <glm/gtc/type_ptr.hpp>

namespace Bellows {

	namespace {

		// The most a model may have: far past any game model, and below what 32-bit indices and memory
		// allow. An accessor without data is all zeros and may claim any count, so a damaged one could
		// otherwise ask for billions of vertices.
		constexpr size_t c_MaxModelVertices = size_t(1) << 25;
		constexpr size_t c_MaxModelIndices = size_t(1) << 27;

		struct GLTFImportContext
		{
			std::filesystem::path BaseDirectory;
			const cgltf_data* Data = nullptr;
			std::map<std::pair<const cgltf_image*, TextureColorSpace>, Ref<Texture2D>> TextureCache;
		};

		std::vector<uint8_t> DecodeDataURI(const char* uri)
		{
			// data:[<mediatype>][;base64],<data>
			const char* comma = std::strchr(uri, ',');
			if (!comma || !std::strstr(uri, ";base64"))
				return {};

			const char* base64 = comma + 1;
			const size_t length = std::strlen(base64);
			// Base64 comes in groups of 4 characters; anything else is damaged (and would give a size
			// below zero, which wraps to a huge one).
			if (length == 0 || length % 4 != 0)
				return {};
			size_t padding = 0;
			if (length >= 1 && base64[length - 1] == '=') padding++;
			if (length >= 2 && base64[length - 2] == '=') padding++;
			const size_t size = length / 4 * 3 - padding;

			void* decoded = nullptr;
			cgltf_options options {};
			if (cgltf_load_buffer_base64(&options, size, base64, &decoded) != cgltf_result_success)
				return {};
			std::vector<uint8_t> bytes(static_cast<uint8_t*>(decoded), static_cast<uint8_t*>(decoded) + size);
			std::free(decoded);
			return bytes;
		}

		// cgltf reads each buffer at the size the file gives it, so a damaged size asks for gigabytes
		// before the read fails. Each must fit in what holds it: its file, or its base64 text.
		bool CheckBufferSizes(const cgltf_data& data, const std::filesystem::path& baseDirectory, std::string& outError)
		{
			for (cgltf_size i = 0; i < data.buffers_count; i++)
			{
				const cgltf_buffer& buffer = data.buffers[i];
				if (!buffer.uri)
					continue;	// The .glb's own binary chunk, which cgltf checks
				if (std::strncmp(buffer.uri, "data:", 5) == 0)
				{
					const char* comma = std::strchr(buffer.uri, ',');
					const size_t available = comma ? std::strlen(comma + 1) / 4 * 3 : 0;
					if (buffer.size > available)
					{
						outError = "buffer " + std::to_string(i) + " says it has " + std::to_string(buffer.size) + " bytes, more than its data holds";
						return false;
					}
					continue;
				}
				std::string uri = buffer.uri;
				cgltf_decode_uri(uri.data());
				uri.resize(std::strlen(uri.c_str()));
				const std::filesystem::path file = baseDirectory / std::filesystem::path(reinterpret_cast<const char8_t*>(uri.c_str()));
				std::error_code error;
				const uintmax_t fileSize = std::filesystem::file_size(file, error);
				if (error || fileSize < buffer.size)
				{
					outError = "buffer file '" + uri + "' " + (error ? std::string("can't be read") : "has " + std::to_string(fileSize) + " bytes; the model says "
						+ std::to_string(buffer.size));
					return false;
				}
			}
			return true;
		}

		// glTF requires every accessor's data to start on a multiple of its component size; cgltf reads
		// the values in place, and a misaligned read is undefined behavior in C++.
		bool CheckAccessorAlignment(const cgltf_data& data, std::string& outError)
		{
			const auto aligned = [](const cgltf_buffer_view* view, cgltf_size offset, cgltf_size componentSize)
			{
				if (!view || !view->buffer || !view->buffer->data || componentSize <= 1)
					return true;
				const uintptr_t address = reinterpret_cast<uintptr_t>(view->buffer->data) + view->offset + offset;
				return address % componentSize == 0 && view->stride % componentSize == 0;
			};
			for (cgltf_size i = 0; i < data.accessors_count; i++)
			{
				const cgltf_accessor& accessor = data.accessors[i];
				const cgltf_size componentSize = cgltf_component_size(accessor.component_type);
				bool ok = aligned(accessor.buffer_view, accessor.offset, componentSize);
				if (accessor.is_sparse)
				{
					ok = ok && aligned(accessor.sparse.indices_buffer_view, accessor.sparse.indices_byte_offset, cgltf_component_size(accessor.sparse.indices_component_type))
						&& aligned(accessor.sparse.values_buffer_view, accessor.sparse.values_byte_offset, componentSize);
				}
				if (!ok)
				{
					outError = "accessor " + std::to_string(i) + "'s data isn't aligned to its " + std::to_string(componentSize) + "-byte values, as glTF requires";
					return false;
				}
			}
			return true;
		}

		Ref<Texture2D> LoadTexture(GLTFImportContext& context, const cgltf_texture_view& view, TextureColorSpace colorSpace)
		{
			if (!view.texture || !view.texture->image)
				return nullptr;

			const cgltf_image* image = view.texture->image;
			const auto key = std::make_pair(image, colorSpace);
			auto it = context.TextureCache.find(key);
			if (it != context.TextureCache.end())
				return it->second;

			const std::string debugName = image->name ? image->name : (image->uri ? image->uri : "EmbeddedTexture");
			Ref<Texture2D> texture;

			if (image->buffer_view)
			{
				const cgltf_buffer_view* bufferView = image->buffer_view;
				const auto* bytes = static_cast<const uint8_t*>(bufferView->buffer->data) + bufferView->offset;
				texture = Texture2D::LoadFromMemory(bytes, bufferView->size, colorSpace, debugName);
			}
			else if (image->uri)
			{
				if (std::strncmp(image->uri, "data:", 5) == 0)
				{
					const std::vector<uint8_t> bytes = DecodeDataURI(image->uri);
					if (!bytes.empty())
						texture = Texture2D::LoadFromMemory(bytes.data(), bytes.size(), colorSpace, debugName);
				}
				else
				{
					std::string uri = image->uri;
					cgltf_decode_uri(uri.data());
					uri.resize(std::strlen(uri.c_str()));
					texture = Texture2D::LoadFromFile(context.BaseDirectory / std::filesystem::path(reinterpret_cast<const char8_t*>(uri.c_str())), colorSpace);
				}
			}

			if (!texture)
				BW_CORE_WARN("glTF: could not load texture '{}'", debugName);
			context.TextureCache[key] = texture;
			return texture;
		}

		Ref<Material> ImportMaterial(GLTFImportContext& context, const cgltf_material& source, size_t index)
		{
			Ref<Material> material = CreateRef<Material>(source.name ? source.name : ("Material" + std::to_string(index)));
			MaterialProperties& properties = material->EditProperties();

			if (source.has_pbr_metallic_roughness)
			{
				const cgltf_pbr_metallic_roughness& pbr = source.pbr_metallic_roughness;
				properties.AlbedoColor = glm::make_vec4(pbr.base_color_factor);
				properties.Metallic = pbr.metallic_factor;
				properties.Roughness = pbr.roughness_factor;
				material->SetAlbedoMap(LoadTexture(context, pbr.base_color_texture, TextureColorSpace::SRGB));
				material->SetMetallicRoughnessMap(LoadTexture(context, pbr.metallic_roughness_texture, TextureColorSpace::Linear));
			}

			material->SetNormalMap(LoadTexture(context, source.normal_texture, TextureColorSpace::Linear));
			if (source.normal_texture.texture)
				properties.NormalStrength = source.normal_texture.scale;
			material->SetOcclusionMap(LoadTexture(context, source.occlusion_texture, TextureColorSpace::Linear));
			material->SetEmissiveMap(LoadTexture(context, source.emissive_texture, TextureColorSpace::SRGB));

			properties.EmissiveColor = glm::make_vec3(source.emissive_factor);
			properties.EmissiveIntensity = source.has_emissive_strength ? source.emissive_strength.emissive_strength : 1.0f;

			switch (source.alpha_mode)
			{
				case cgltf_alpha_mode_mask:		properties.Mode = AlphaMode::Mask; break;
				case cgltf_alpha_mode_blend:	properties.Mode = AlphaMode::Blend; break;
				default:						properties.Mode = AlphaMode::Opaque; break;
			}
			properties.AlphaCutoff = source.alpha_cutoff;
			properties.DoubleSided = source.double_sided;
			return material;
		}

		bool ImportPrimitive(const cgltf_primitive& primitive, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, Submesh& submesh)
		{
			if (primitive.type != cgltf_primitive_type_triangles)
				return false;

			const cgltf_accessor* positions = nullptr;
			const cgltf_accessor* normals = nullptr;
			const cgltf_accessor* tangents = nullptr;
			const cgltf_accessor* texCoords = nullptr;
			for (cgltf_size i = 0; i < primitive.attributes_count; i++)
			{
				const cgltf_attribute& attribute = primitive.attributes[i];
				switch (attribute.type)
				{
					case cgltf_attribute_type_position:	positions = attribute.data; break;
					case cgltf_attribute_type_normal:	normals = attribute.data; break;
					case cgltf_attribute_type_tangent:	tangents = attribute.data; break;
					case cgltf_attribute_type_texcoord:	if (attribute.index == 0) texCoords = attribute.data; break;
					default: break;
				}
			}
			if (!positions || positions->count == 0)
				return false;
			const size_t indexCount = primitive.indices ? primitive.indices->count : positions->count;
			if (positions->count > c_MaxModelVertices - std::min(vertices.size(), c_MaxModelVertices)
				|| indexCount > c_MaxModelIndices - std::min(indices.size(), c_MaxModelIndices))
			{
				BW_CORE_ERROR("glTF: a mesh has {} vertices and {} indices; a model can have up to {} and {}", positions->count, indexCount,
					c_MaxModelVertices, c_MaxModelIndices);
				return false;
			}

			const size_t vertexCount = positions->count;
			std::vector<Vertex> primitiveVertices(vertexCount);
			for (size_t i = 0; i < vertexCount; i++)
			{
				Vertex& vertex = primitiveVertices[i];
				cgltf_accessor_read_float(positions, i, glm::value_ptr(vertex.Position), 3);
				if (normals)
					cgltf_accessor_read_float(normals, i, glm::value_ptr(vertex.Normal), 3);
				if (tangents)
					cgltf_accessor_read_float(tangents, i, glm::value_ptr(vertex.Tangent), 4);
				if (texCoords)
					cgltf_accessor_read_float(texCoords, i, glm::value_ptr(vertex.TexCoord), 2);
			}

			std::vector<uint32_t> primitiveIndices;
			if (primitive.indices)
			{
				primitiveIndices.resize(primitive.indices->count);
				for (size_t i = 0; i < primitiveIndices.size(); i++)
					primitiveIndices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i));
			}
			else
			{
				primitiveIndices.resize(vertexCount);
				for (size_t i = 0; i < vertexCount; i++)
					primitiveIndices[i] = static_cast<uint32_t>(i);
			}

			// Drop incomplete triangles and out-of-range indices.
			primitiveIndices.resize(primitiveIndices.size() - primitiveIndices.size() % 3);
			for (uint32_t& index : primitiveIndices)
			{
				if (index >= vertexCount)
					index = 0;
			}

			if (!normals)
			{
				for (Vertex& vertex : primitiveVertices)
					vertex.Normal = glm::vec3(0.0f);
				for (size_t i = 0; i + 2 < primitiveIndices.size(); i += 3)
				{
					Vertex& a = primitiveVertices[primitiveIndices[i]];
					Vertex& b = primitiveVertices[primitiveIndices[i + 1]];
					Vertex& c = primitiveVertices[primitiveIndices[i + 2]];
					const glm::vec3 faceNormal = glm::cross(b.Position - a.Position, c.Position - a.Position);
					a.Normal += faceNormal;
					b.Normal += faceNormal;
					c.Normal += faceNormal;
				}
				for (Vertex& vertex : primitiveVertices)
				{
					const float length = glm::length(vertex.Normal);
					vertex.Normal = length > 1e-12f ? vertex.Normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
				}
			}
			else
			{
				for (Vertex& vertex : primitiveVertices)
				{
					const float length = glm::length(vertex.Normal);
					vertex.Normal = length > 1e-12f ? vertex.Normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
				}
			}

			if (!tangents && texCoords)
				Mesh::GenerateTangents(primitiveVertices, primitiveIndices);

			submesh.BaseVertex = static_cast<uint32_t>(vertices.size());
			submesh.BaseIndex = static_cast<uint32_t>(indices.size());
			submesh.VertexCount = static_cast<uint32_t>(primitiveVertices.size());
			submesh.IndexCount = static_cast<uint32_t>(primitiveIndices.size());

			vertices.insert(vertices.end(), primitiveVertices.begin(), primitiveVertices.end());
			indices.insert(indices.end(), primitiveIndices.begin(), primitiveIndices.end());
			return submesh.IndexCount > 0;
		}

	}

	const char* ModelFrontToString(ModelFront front)
	{
		return front == ModelFront::PositiveZ ? "+Z" : "-Z";
	}

	std::optional<ModelFront> ModelFrontFromString(std::string_view text)
	{
		if (text == "-Z")
			return ModelFront::NegativeZ;
		if (text == "+Z")
			return ModelFront::PositiveZ;
		return std::nullopt;
	}

	std::filesystem::path MeshImportSettings::GetFilePath(const std::filesystem::path& modelPath)
	{
		std::filesystem::path path = modelPath;
		path += ".bimport";
		return path;
	}

	std::optional<MeshImportSettings> MeshImportSettings::Load(const std::filesystem::path& modelPath)
	{
		const std::filesystem::path file = GetFilePath(modelPath);
		std::error_code error;
		if (!std::filesystem::is_regular_file(file, error))
			return std::nullopt;
		const std::optional<std::string> text = FileSystem::ReadTextFile(file);
		const Json json = text ? Json::parse(*text, nullptr, false) : Json();
		if (!json.is_object())
		{
			BW_CORE_WARN("Import settings '{}' could not be read; using the project's defaults", file.string());
			return std::nullopt;
		}
		MeshImportSettings settings;
		std::string front = ModelFrontToString(settings.Front);
		JsonRead(json, "FrontFaces", front);
		if (const std::optional<ModelFront> parsed = ModelFrontFromString(front))
			settings.Front = *parsed;
		else
			BW_CORE_WARN("Import settings '{}': FrontFaces is \"{}\"; it must be \"-Z\" or \"+Z\"", file.string(), front);
		return settings;
	}

	bool MeshImportSettings::Save(const std::filesystem::path& modelPath) const
	{
		Json json;
		json["Version"] = 1;
		json["FrontFaces"] = ModelFrontToString(Front);
		return FileSystem::WriteTextFile(GetFilePath(modelPath), json.dump(1, '\t') + "\n");
	}

	Ref<Mesh> MeshImporter::ImportGLTF(const std::filesystem::path& path, const MeshImportSettings& settings)
	{
		const std::string pathString = path.string();
		cgltf_options options {};
		cgltf_data* data = nullptr;

		cgltf_result result = cgltf_parse_file(&options, pathString.c_str(), &data);
		if (result != cgltf_result_success)
		{
			BW_CORE_ERROR("glTF: failed to parse '{}' (error {})", pathString, static_cast<int>(result));
			return nullptr;
		}

		std::unique_ptr<cgltf_data, decltype(&cgltf_free)> dataGuard(data, &cgltf_free);

		std::string error;
		if (!CheckBufferSizes(*data, path.parent_path(), error))
		{
			BW_CORE_ERROR("glTF: '{}' can't be loaded: {}", pathString, error);
			return nullptr;
		}
		result = cgltf_load_buffers(&options, data, pathString.c_str());
		if (result != cgltf_result_success)
		{
			BW_CORE_ERROR("glTF: failed to load buffers of '{}' (error {})", pathString, static_cast<int>(result));
			return nullptr;
		}
		if (!CheckAccessorAlignment(*data, error))
		{
			BW_CORE_ERROR("glTF: '{}' can't be loaded: {}", pathString, error);
			return nullptr;
		}
		if (cgltf_validate(data) != cgltf_result_success)
		{
			BW_CORE_ERROR("glTF: '{}' failed validation", pathString);
			return nullptr;
		}

		GLTFImportContext context;
		context.BaseDirectory = path.parent_path();
		context.Data = data;

		std::vector<Ref<Material>> materials;
		materials.reserve(data->materials_count + 1);
		for (cgltf_size i = 0; i < data->materials_count; i++)
			materials.push_back(ImportMaterial(context, data->materials[i], i));
		const uint32_t defaultMaterialIndex = static_cast<uint32_t>(materials.size());
		materials.push_back(CreateRef<Material>("Default"));

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		std::vector<Submesh> submeshes;
		// A +Z-front model turns half a turn about +Y, around its own origin: x and z change sign.
		const glm::mat4 turn = settings.Front == ModelFront::PositiveZ
			? glm::mat4(glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f), glm::vec4(0.0f, 1.0f, 0.0f, 0.0f), glm::vec4(0.0f, 0.0f, -1.0f, 0.0f), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f))
			: glm::mat4(1.0f);

		auto importNode = [&](const cgltf_node* node)
		{
			if (!node->mesh)
				return;
			glm::mat4 worldTransform(1.0f);
			cgltf_node_transform_world(node, glm::value_ptr(worldTransform));

			for (cgltf_size p = 0; p < node->mesh->primitives_count; p++)
			{
				const cgltf_primitive& primitive = node->mesh->primitives[p];
				Submesh submesh;
				if (!ImportPrimitive(primitive, vertices, indices, submesh))
					continue;
				submesh.Transform = turn * worldTransform;
				submesh.MaterialIndex = primitive.material
					? static_cast<uint32_t>(cgltf_material_index(data, primitive.material)) : defaultMaterialIndex;
				submesh.Name = node->name ? node->name : (node->mesh->name ? node->mesh->name : "Submesh");
				submeshes.push_back(submesh);
			}
		};

		const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
		if (scene)
		{
			std::function<void(const cgltf_node*)> visit = [&](const cgltf_node* node)
			{
				importNode(node);
				for (cgltf_size i = 0; i < node->children_count; i++)
					visit(node->children[i]);
			};
			for (cgltf_size i = 0; i < scene->nodes_count; i++)
				visit(scene->nodes[i]);
		}
		else
		{
			for (cgltf_size i = 0; i < data->nodes_count; i++)
				importNode(&data->nodes[i]);
		}

		if (submeshes.empty())
		{
			BW_CORE_ERROR("glTF: '{}' contains no triangle meshes", pathString);
			return nullptr;
		}

		BW_CORE_INFO("Imported '{}': {} submeshes, {} vertices, {} materials", path.filename().string(), submeshes.size(), vertices.size(), materials.size() - 1);
		return CreateRef<Mesh>(path.stem().string(), std::move(vertices), std::move(indices), std::move(submeshes), std::move(materials));
	}

}
