#include "bwpch.h"
#include "Bellows/Asset/MeshImporter.h"

#include "Bellows/Core/FileSystem.h"
#include "Bellows/Renderer/Texture.h"

#include <cgltf.h>
#include <glm/gtc/type_ptr.hpp>

namespace Bellows {

	namespace {

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

	Ref<Mesh> MeshImporter::ImportGLTF(const std::filesystem::path& path)
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

		result = cgltf_load_buffers(&options, data, pathString.c_str());
		if (result != cgltf_result_success)
		{
			BW_CORE_ERROR("glTF: failed to load buffers of '{}' (error {})", pathString, static_cast<int>(result));
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
				submesh.Transform = worldTransform;
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
