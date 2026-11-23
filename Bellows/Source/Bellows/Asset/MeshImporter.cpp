#include "bwpch.h"
#include "Bellows/Asset/MeshImporter.h"

#include "Bellows/Core/FileSystem.h"
#include "Bellows/Renderer/Texture.h"
#include "Bellows/Serialization/JsonUtils.h"

#include <cgltf.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <optional>
#include <unordered_map>

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

		// Extensions a file may require: the ones the importer reads (emissive strength) or that cgltf's
		// accessor reads already handle (quantized vertex data).
		constexpr std::string_view c_SupportedRequiredExtensions[] = { "KHR_materials_emissive_strength", "KHR_mesh_quantization" };

		// The skeleton of one glTF skin: its joints in parents-first order, and where each node went.
		struct ImportedSkin
		{
			const cgltf_skin* Source = nullptr;
			Ref<Skeleton> Rig;
			std::vector<uint32_t> JointOfSkinIndex;			// skin->joints[i] -> our joint index
			std::unordered_map<const cgltf_node*, uint32_t> JointOfNode;
		};

		JointPose LocalPose(const cgltf_node& node)
		{
			JointPose pose;
			if (node.has_matrix)
			{
				glm::mat4 matrix(1.0f);
				std::memcpy(glm::value_ptr(matrix), node.matrix, sizeof(float) * 16);
				glm::vec3 skew(0.0f);
				glm::vec4 perspective(0.0f);
				glm::decompose(matrix, pose.Scale, pose.Rotation, pose.Translation, skew, perspective);
				return pose;
			}
			if (node.has_translation)
				pose.Translation = glm::make_vec3(node.translation);
			if (node.has_rotation)
				pose.Rotation = glm::normalize(glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]));
			if (node.has_scale)
				pose.Scale = glm::make_vec3(node.scale);
			return pose;
		}

		glm::mat4 LocalMatrix(const cgltf_node& node)
		{
			glm::mat4 matrix(1.0f);
			cgltf_node_transform_local(&node, glm::value_ptr(matrix));
			return matrix;
		}

		std::optional<ImportedSkin> ImportSkin(const cgltf_skin& skin)
		{
			if (skin.joints_count == 0)
				return std::nullopt;
			if (skin.joints_count > Skeleton::MaxJoints)
			{
				BW_CORE_WARN("glTF: a skin has {} joints and a model can have up to {}, so it is imported without animation", skin.joints_count, Skeleton::MaxJoints);
				return std::nullopt;
			}

			std::unordered_map<const cgltf_node*, cgltf_size> skinIndex;
			for (cgltf_size i = 0; i < skin.joints_count; i++)
			{
				if (!skin.joints[i] || !skinIndex.emplace(skin.joints[i], i).second)
				{
					BW_CORE_WARN("glTF: a skin lists a joint twice or not at all, so the model is imported without animation");
					return std::nullopt;
				}
			}

			// Each joint's nearest ancestor that is a joint too, and how deep it is among joints, so
			// sorting by depth puts parents first.
			struct Found { const cgltf_node* Node; const cgltf_node* ParentJoint; int Depth; glm::mat4 Between; cgltf_size Index; };
			std::vector<Found> found;
			for (cgltf_size i = 0; i < skin.joints_count; i++)
			{
				const cgltf_node* node = skin.joints[i];
				glm::mat4 between(1.0f);
				const cgltf_node* parent = node->parent;
				while (parent && !skinIndex.contains(parent))
				{
					between = LocalMatrix(*parent) * between;
					parent = parent->parent;
				}
				int depth = 0;
				for (const cgltf_node* up = parent; up; up = up->parent)
					depth += skinIndex.contains(up) ? 1 : 0;
				found.push_back({ node, parent, depth, between, i });
			}
			std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.Depth < b.Depth; });

			ImportedSkin result;
			result.Source = &skin;
			result.JointOfSkinIndex.resize(skin.joints_count);
			for (size_t j = 0; j < found.size(); j++)
			{
				result.JointOfNode[found[j].Node] = static_cast<uint32_t>(j);
				result.JointOfSkinIndex[found[j].Index] = static_cast<uint32_t>(j);
			}

			std::vector<Joint> joints(found.size());
			for (size_t j = 0; j < found.size(); j++)
			{
				Joint& joint = joints[j];
				joint.Name = found[j].Node->name ? found[j].Node->name : "Joint " + std::to_string(j);
				joint.Parent = found[j].ParentJoint ? static_cast<int32_t>(result.JointOfNode.at(found[j].ParentJoint)) : -1;
				joint.Rest = LocalPose(*found[j].Node);
				joint.ParentTransform = found[j].Between;
				if (skin.inverse_bind_matrices && skin.inverse_bind_matrices->count >= skin.joints_count)
				{
					glm::mat4 inverseBind(1.0f);
					cgltf_accessor_read_float(skin.inverse_bind_matrices, found[j].Index, glm::value_ptr(inverseBind), 16);
					const float* values = glm::value_ptr(inverseBind);
					if (std::all_of(values, values + 16, [](float value) { return std::isfinite(value); }))
						joint.InverseBind = inverseBind;
				}
			}
			result.Rig = CreateRef<Skeleton>(std::move(joints));
			return result;
		}

		// A primitive's joints and weights, as our joints; vertices without usable weights follow the first joint.
		void ImportSkinVertices(const cgltf_primitive& primitive, const ImportedSkin& skin, size_t vertexCount, std::vector<SkinVertex>& outVertices)
		{
			const cgltf_accessor* jointData = nullptr;
			const cgltf_accessor* weightData = nullptr;
			for (cgltf_size i = 0; i < primitive.attributes_count; i++)
			{
				const cgltf_attribute& attribute = primitive.attributes[i];
				if (attribute.type == cgltf_attribute_type_joints && attribute.index == 0)
					jointData = attribute.data;
				else if (attribute.type == cgltf_attribute_type_weights && attribute.index == 0)
					weightData = attribute.data;
			}
			const size_t start = outVertices.size();
			outVertices.resize(start + vertexCount);
			if (!jointData || !weightData || jointData->count < vertexCount || weightData->count < vertexCount)
				return;
			for (size_t v = 0; v < vertexCount; v++)
			{
				cgltf_uint joints[4] = {};
				glm::vec4 weights(0.0f);
				cgltf_accessor_read_uint(jointData, v, joints, 4);
				cgltf_accessor_read_float(weightData, v, glm::value_ptr(weights), 4);
				SkinVertex& vertex = outVertices[start + v];
				float total = 0.0f;
				for (int k = 0; k < 4; k++)
				{
					const bool usable = joints[k] < skin.JointOfSkinIndex.size() && std::isfinite(weights[k]) && weights[k] > 0.0f;
					vertex.Joints[k] = usable ? static_cast<uint8_t>(skin.JointOfSkinIndex[joints[k]]) : 0;
					vertex.Weights[k] = usable ? weights[k] : 0.0f;
					total += vertex.Weights[k];
				}
				vertex.Weights = total > 1e-6f ? vertex.Weights / total : glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
			}
		}

		// The animations that move the skin's joints, as clips with unique names.
		std::vector<Ref<AnimationClip>> ImportClips(const cgltf_data& data, const ImportedSkin& skin)
		{
			std::vector<Ref<AnimationClip>> clips;
			for (cgltf_size a = 0; a < data.animations_count && clips.size() < AnimationClip::MaxClips; a++)
			{
				const cgltf_animation& animation = data.animations[a];
				std::vector<AnimationTrack> tracks;
				for (cgltf_size c = 0; c < animation.channels_count; c++)
				{
					const cgltf_animation_channel& channel = animation.channels[c];
					const cgltf_animation_sampler* sampler = channel.sampler;
					const auto joint = skin.JointOfNode.find(channel.target_node);
					if (!sampler || !sampler->input || !sampler->output || joint == skin.JointOfNode.end())
						continue;
					AnimationTrack track;
					track.Joint = joint->second;
					switch (channel.target_path)
					{
						case cgltf_animation_path_type_translation:	track.Path = AnimationPath::Translation; break;
						case cgltf_animation_path_type_rotation:	track.Path = AnimationPath::Rotation; break;
						case cgltf_animation_path_type_scale:		track.Path = AnimationPath::Scale; break;
						default: continue;	// Morph target weights aren't supported
					}
					switch (sampler->interpolation)
					{
						case cgltf_interpolation_type_step:			track.Interpolation = AnimationInterpolation::Step; break;
						case cgltf_interpolation_type_cubic_spline:	track.Interpolation = AnimationInterpolation::CubicSpline; break;
						default:									track.Interpolation = AnimationInterpolation::Linear; break;
					}
					const size_t keys = sampler->input->count;
					const size_t valuesPerKey = track.Interpolation == AnimationInterpolation::CubicSpline ? 3 : 1;
					const size_t components = track.Path == AnimationPath::Rotation ? 4 : 3;
					if (keys == 0 || sampler->output->count != keys * valuesPerKey || cgltf_num_components(sampler->output->type) != components)
						continue;

					track.Times.resize(keys);
					bool ordered = true;
					for (size_t k = 0; k < keys && ordered; k++)
					{
						cgltf_accessor_read_float(sampler->input, k, &track.Times[k], 1);
						ordered = std::isfinite(track.Times[k]) && track.Times[k] >= 0.0f && (k == 0 || track.Times[k] >= track.Times[k - 1]);
					}
					track.Values.resize(keys * valuesPerKey, glm::vec4(0.0f));
					for (size_t k = 0; k < track.Values.size() && ordered; k++)
					{
						cgltf_accessor_read_float(sampler->output, k, glm::value_ptr(track.Values[k]), components);
						ordered = glm::all(glm::lessThan(glm::abs(track.Values[k]), glm::vec4(std::numeric_limits<float>::max())));
					}
					if (!ordered)
					{
						BW_CORE_WARN("glTF: a track of animation '{}' has times out of order or values that aren't numbers, so it is left out",
							animation.name ? animation.name : "");
						continue;
					}
					tracks.push_back(std::move(track));
				}
				if (tracks.empty())
					continue;

				std::string name = animation.name && animation.name[0] ? animation.name : "Animation " + std::to_string(a + 1);
				const std::string base = name;
				for (int copy = 2; std::any_of(clips.begin(), clips.end(), [&name](const Ref<AnimationClip>& clip) { return clip->GetName() == name; }); copy++)
					name = base + " " + std::to_string(copy);
				clips.push_back(CreateRef<AnimationClip>(std::move(name), std::move(tracks)));
			}
			return clips;
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

		// A file that needs an extension Bellows doesn't read would come out wrong (compressed geometry
		// missing, say), so it's refused with the reason. Extensions a file only uses are fine to skip.
		for (cgltf_size i = 0; i < data->extensions_required_count; i++)
		{
			const std::string_view name = data->extensions_required[i] ? data->extensions_required[i] : "";
			if (std::find(std::begin(c_SupportedRequiredExtensions), std::end(c_SupportedRequiredExtensions), name) == std::end(c_SupportedRequiredExtensions))
			{
				BW_CORE_ERROR("glTF: '{}' needs the extension '{}', which Bellows can't read. Export it without that extension "
					"(in Blender: without Draco or meshopt compression)", pathString, name);
				return nullptr;
			}
		}

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

		// The first skin a mesh node uses becomes the model's skeleton; nodes with another skin are
		// imported in their rest pose.
		std::optional<ImportedSkin> skin;
		std::vector<SkinVertex> skinVertices;
		for (cgltf_size i = 0; i < data->nodes_count && !skin; i++)
		{
			if (data->nodes[i].mesh && data->nodes[i].skin)
				skin = ImportSkin(*data->nodes[i].skin);
		}

		auto importNode = [&](const cgltf_node* node)
		{
			if (!node->mesh)
				return;
			glm::mat4 worldTransform(1.0f);
			cgltf_node_transform_world(node, glm::value_ptr(worldTransform));
			const bool skinned = skin && node->skin == skin->Source;

			for (cgltf_size p = 0; p < node->mesh->primitives_count; p++)
			{
				const cgltf_primitive& primitive = node->mesh->primitives[p];
				Submesh submesh;
				if (!ImportPrimitive(primitive, vertices, indices, submesh))
					continue;
				if (skin)
				{
					// Weights line up with the vertices: this primitive's come last.
					skinVertices.resize(vertices.size() - submesh.VertexCount);
					if (skinned)
						ImportSkinVertices(primitive, *skin, submesh.VertexCount, skinVertices);
					else
						skinVertices.resize(vertices.size());
				}
				// A skinned primitive is placed by its joints (glTF ignores its node's transform); the skeleton
				// stays in the file's space, so the model's turn is applied after skinning.
				submesh.Skinned = skinned;
				submesh.Transform = skinned ? turn : turn * worldTransform;
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
		Ref<Mesh> mesh = CreateRef<Mesh>(path.stem().string(), std::move(vertices), std::move(indices), std::move(submeshes), std::move(materials));
		if (skin && std::any_of(mesh->GetSubmeshes().begin(), mesh->GetSubmeshes().end(), [](const Submesh& submesh) { return submesh.Skinned; }))
		{
			MeshSkin meshSkin;
			meshSkin.Rig = skin->Rig;
			meshSkin.Vertices = std::move(skinVertices);
			meshSkin.Clips = ImportClips(*data, *skin);
			BW_CORE_INFO("'{}' is skinned: {} joints, {} animations", path.filename().string(), meshSkin.Rig->GetJointCount(), meshSkin.Clips.size());
			mesh->SetSkin(std::move(meshSkin));
		}
		return mesh;
	}

}
