// Copyright 2026+ the openage authors. See copying.md for legal info.

module;

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "error/error.h"
#include "log/message.h"
#include "renderer/geometry.h"
#include "renderer/render_pass.h"
#include "renderer/renderer.h"
#include "renderer/resources/texture_data.h"
#include "renderer/uniform_input.h"

export module openage.renderer.recording;

import openage.util.snapshot_text;

export namespace openage::renderer {

/** CPU command payload; coordinates and fill are exact hundredths, never GPU handles. */
struct RecordingCommand {
	enum class Kind {
		SPRITE,
		UI
	};
	Kind kind;
	std::string id;
	int64_t x;
	int64_t y;
	int64_t z{0};
	std::string tint{"#FFFFFF"};
	std::array<int64_t, 4> rect{};
	int64_t fill{100};
};

/** Typed CPU uniform input for semantic sprite/UI recording. */
class RecordingUniformInput final : public UniformInput {
public:
	explicit RecordingUniformInput(RecordingCommand command) :
		UniformInput{nullptr}, command{std::move(command)} {}
	bool is_complete() const override {
		return true;
	}
	RecordingCommand command;
	void update() override {
		throw Error{MSG(err) << "Use typed recording commands"};
	}
	void update(const char *, int32_t) override {
		throw Error{MSG(err) << "Use typed recording commands"};
	}
	void update(const char *, uint32_t) override {
		throw Error{MSG(err) << "Use typed recording commands"};
	}
	void update(const char *, float) override {
		throw Error{MSG(err) << "Use typed recording commands"};
	}
	void update(const char *, double) override {
		throw Error{MSG(err) << "Use typed recording commands"};
	}
	void update(const char *, bool) override {
		throw Error{MSG(err) << "Use typed recording commands"};
	}
	void update(const char *, Eigen::Vector2f const &) override {
		throw Error{MSG(err) << "Use typed recording commands"};
	}
	void update(const char *, Eigen::Vector3f const &) override {
		throw Error{MSG(err) << "Use typed recording commands"};
	}
	void update(const char *, Eigen::Vector4f const &) override {
		throw Error{MSG(err) << "Use typed recording commands"};
	}
	void update(const char *, Eigen::Vector2i const &) override {
		throw Error{MSG(err) << "Use typed recording commands"};
	}
	void update(const char *, Eigen::Vector3i const &) override {
		throw Error{MSG(err) << "Use typed recording commands"};
	}
	void update(const char *, Eigen::Vector4i const &) override {
		throw Error{MSG(err) << "Use typed recording commands"};
	}
	void update(const char *, Eigen::Vector2<uint32_t> const &) override {
		throw Error{MSG(err) << "Use typed recording commands"};
	}
	void update(const char *, Eigen::Vector3<uint32_t> const &) override {
		throw Error{MSG(err) << "Use typed recording commands"};
	}
	void update(const char *, Eigen::Vector4<uint32_t> const &) override {
		throw Error{MSG(err) << "Use typed recording commands"};
	}
	void update(const char *, std::shared_ptr<Texture2d> const &) override {
		throw Error{MSG(err) << "Use typed recording commands"};
	}
	void update(const char *, std::shared_ptr<Texture2d> &) override {
		throw Error{MSG(err) << "Use typed recording commands"};
	}
	void update(const char *, Eigen::Matrix4f const &) override {
		throw Error{MSG(err) << "Use typed recording commands"};
	}
};

/** Opt-in CPU Renderer backend. GPU resource requests fail explicitly. */
class RecordingRenderer final : public Renderer {
public:
	/** Clear the previous frame; subsequent passes append in submission order. */
	void begin_frame() {
		this->text.clear();
	}
	/** Return canonical LF-terminated commands from this frame. */
	const std::string &get_text() const {
		return this->text;
	}
	void check_error() override {}

	std::shared_ptr<RenderPass> add_render_pass(std::vector<Renderable> renderables,
	                                            const std::shared_ptr<RenderTarget> &target) override;
	void render(const std::shared_ptr<RenderPass> &pass) override;

	std::shared_ptr<Texture2d> add_texture(resources::Texture2dData const &) override {
		throw Error{MSG(err) << "RecordingRenderer does not support GPU resources"};
	}

	std::shared_ptr<Texture2d> add_texture(resources::Texture2dInfo const &) override {
		throw Error{MSG(err) << "RecordingRenderer does not support GPU resources"};
	}

	std::shared_ptr<ShaderProgram> add_shader(std::vector<resources::ShaderSource> const &) override {
		throw Error{MSG(err) << "RecordingRenderer does not support GPU resources"};
	}

	std::shared_ptr<Geometry> add_mesh_geometry(resources::MeshData const &) override {
		throw Error{MSG(err) << "RecordingRenderer does not support GPU resources"};
	}

	std::shared_ptr<Geometry> add_bufferless_quad() override;

	std::shared_ptr<RenderTarget> create_texture_target(std::vector<std::shared_ptr<Texture2d>> const &) override {
		throw Error{MSG(err) << "RecordingRenderer does not support GPU resources"};
	}

	std::shared_ptr<RenderTarget> get_display_target() override {
		throw Error{MSG(err) << "RecordingRenderer does not support GPU resources"};
	}

	std::shared_ptr<UniformBuffer> add_uniform_buffer(resources::UniformBufferInfo const &) override {
		throw Error{MSG(err) << "RecordingRenderer does not support GPU resources"};
	}

	std::shared_ptr<UniformBuffer> add_uniform_buffer(std::shared_ptr<ShaderProgram> const &, std::string const &) override {
		throw Error{MSG(err) << "RecordingRenderer does not support GPU resources"};
	}

	resources::Texture2dData display_into_data() override {
		throw Error{MSG(err) << "RecordingRenderer does not support GPU resources"};
	}


private:
	std::string text;
};

} // namespace openage::renderer

namespace openage::renderer {

namespace {
class RecordingGeometry final : public Geometry {
public:
	RecordingGeometry() : Geometry{geometry_t::bufferless_quad} {}
	void update_verts_offset(const std::vector<uint8_t> &, size_t) override {
		throw Error{MSG(err) << "Recording quad has no vertex buffer"};
	}
};

class RecordingPass final : public RenderPass {
public:
	RecordingPass(std::vector<Renderable> renderables, const std::shared_ptr<RenderTarget> &target) :
		RenderPass{std::move(renderables), target} {}
};
} // namespace

std::shared_ptr<Geometry> RecordingRenderer::add_bufferless_quad() {
	return std::make_shared<RecordingGeometry>();
}

std::shared_ptr<RenderPass> RecordingRenderer::add_render_pass(std::vector<Renderable> renderables,
                                                               const std::shared_ptr<RenderTarget> &target) {
	return std::make_shared<RecordingPass>(std::move(renderables), target);
}

void RecordingRenderer::render(const std::shared_ptr<RenderPass> &pass) {
	if (not pass) {
		throw Error{MSG(err) << "RecordingRenderer requires a render pass"};
	}
	for (const auto &layer : pass->get_renderables()) {
		for (const auto &renderable : layer) {
			if (not renderable.geometry) {
				continue;
			}
			if (not std::dynamic_pointer_cast<RecordingGeometry>(renderable.geometry)) {
				throw Error{MSG(err) << "RecordingRenderer requires CPU geometry"};
			}
			auto input = std::dynamic_pointer_cast<RecordingUniformInput>(renderable.uniform);
			if (not input) {
				throw Error{MSG(err) << "RecordingRenderer requires CPU recording uniforms"};
			}
			const auto &c = input->command;
			if (c.id.empty() or c.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") != std::string::npos) {
				throw Error{MSG(err) << "Invalid recording command ID"};
			}
			if (c.tint.size() != 7 or c.tint.front() != '#'
			    or c.tint.find_first_not_of("0123456789ABCDEF", 1) != std::string::npos
			    or c.fill < 0 or c.fill > 100) {
				throw Error{MSG(err) << "Invalid recording tint or fill"};
			}
			if (c.kind == RecordingCommand::Kind::SPRITE) {
				this->text += "DRAW_SPRITE id=" + c.id + " pos=(" + util::format_centi(c.x)
				              + "," + util::format_centi(c.y) + ") z=" + std::to_string(c.z)
				              + " tint=" + c.tint + "\n";
			}
			else if (c.kind == RecordingCommand::Kind::UI) {
				this->text += "DRAW_UI id=" + c.id + " rect=(" + std::to_string(c.rect[0])
				              + "," + std::to_string(c.rect[1]) + "," + std::to_string(c.rect[2])
				              + "," + std::to_string(c.rect[3]) + ") fill=" + util::format_centi(c.fill) + "\n";
			}
			else {
				throw Error{MSG(err) << "Unknown recording command"};
			}
		}
	}
}

} // namespace openage::renderer
