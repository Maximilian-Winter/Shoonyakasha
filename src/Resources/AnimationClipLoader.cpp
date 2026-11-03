//
// AnimationClipLoader.cpp - Read animation clips from glTF / GLB files
//

#include "Resources/AnimationClipLoader.h"
#include <cgltf.h>
#include <algorithm>
#include <stdexcept>
#include <string>

namespace Shoonyakasha {

namespace {

AnimationInterpolation interpolationOf(const cgltf_animation_sampler& sampler) {
    switch (sampler.interpolation) {
        case cgltf_interpolation_type_step:         return AnimationInterpolation::Step;
        case cgltf_interpolation_type_cubic_spline: return AnimationInterpolation::CubicSpline;
        default:                                    return AnimationInterpolation::Linear;
    }
}

} // namespace

std::vector<std::shared_ptr<AnimationClip>> readGltfAnimations(const cgltf_data* data, bool withWeights) {
    std::vector<std::shared_ptr<AnimationClip>> clips;
    if (!data) return clips;

    for (cgltf_size animIdx = 0; animIdx < data->animations_count; ++animIdx) {
        const cgltf_animation& anim = data->animations[animIdx];

        auto clip = std::make_shared<AnimationClip>();
        clip->name = anim.name ? anim.name : ("animation_" + std::to_string(animIdx));
        clip->duration = 0.0f;

        for (cgltf_size chanIdx = 0; chanIdx < anim.channels_count; ++chanIdx) {
            const cgltf_animation_channel& channel = anim.channels[chanIdx];
            const cgltf_animation_sampler& sampler = *channel.sampler;
            if (!channel.target_node) continue;

            // Morph target weights of a mesh node.
            if (channel.target_path == cgltf_animation_path_type_weights) {
                const cgltf_mesh* mesh = channel.target_node->mesh;
                if (!withWeights || !mesh || mesh->primitives_count == 0) continue;
                MorphWeightChannel weights;
                weights.targetNode = static_cast<int>(cgltf_node_index(data, channel.target_node));
                weights.interpolation = interpolationOf(sampler);
                weights.weightCount = static_cast<uint32_t>(mesh->primitives[0].targets_count);
                weights.timestamps.resize(sampler.input->count);
                for (cgltf_size t = 0; t < sampler.input->count; ++t) {
                    cgltf_accessor_read_float(sampler.input, t, &weights.timestamps[t], 1);
                    clip->duration = std::max(clip->duration, weights.timestamps[t]);
                }
                weights.values.resize(sampler.output->count);
                for (cgltf_size v = 0; v < sampler.output->count; ++v) {
                    cgltf_accessor_read_float(sampler.output, v, &weights.values[v], 1);
                }
                if (weights.weightCount > 0) clip->weightChannels.push_back(std::move(weights));
                continue;
            }

            AnimationChannel animChannel;
            animChannel.targetJointName = channel.target_node->name ? channel.target_node->name : "";
            switch (channel.target_path) {
                case cgltf_animation_path_type_translation:
                    animChannel.property = AnimationChannel::Property::Translation;
                    break;
                case cgltf_animation_path_type_rotation:
                    animChannel.property = AnimationChannel::Property::Rotation;
                    break;
                case cgltf_animation_path_type_scale:
                    animChannel.property = AnimationChannel::Property::Scale;
                    break;
                default:
                    continue;
            }
            animChannel.interpolation = interpolationOf(sampler);

            const cgltf_accessor* input = sampler.input;
            animChannel.timestamps.resize(input->count);
            for (cgltf_size t = 0; t < input->count; ++t) {
                cgltf_accessor_read_float(input, t, &animChannel.timestamps[t], 1);
                clip->duration = std::max(clip->duration, animChannel.timestamps[t]);
            }

            const cgltf_accessor* output = sampler.output;
            const uint32_t components =
                animChannel.property == AnimationChannel::Property::Rotation ? 4 : 3;
            animChannel.values.resize(output->count);
            for (cgltf_size v = 0; v < output->count; ++v) {
                cgltf_float c[4] = {0, 0, 0, 0};
                cgltf_accessor_read_float(output, v, c, components);
                animChannel.values[v] = glm::vec4(c[0], c[1], c[2], components == 4 ? c[3] : 0.0f);
            }

            clip->channels.push_back(std::move(animChannel));
        }

        if (!clip->channels.empty() || !clip->weightChannels.empty()) {
            clips.push_back(std::move(clip));
        }
    }
    return clips;
}

std::vector<std::shared_ptr<AnimationClip>> loadAnimationClips(const std::filesystem::path& path) {
    const std::string file = path.string();
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, file.c_str(), &data) != cgltf_result_success) {
        throw std::runtime_error("cannot parse glTF file: " + file);
    }
    if (cgltf_load_buffers(&options, data, file.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        throw std::runtime_error("cannot load the buffers of glTF file: " + file);
    }
    auto clips = readGltfAnimations(data, false);
    cgltf_free(data);
    return clips;
}

} // namespace Shoonyakasha
