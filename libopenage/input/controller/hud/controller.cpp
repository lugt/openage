// Copyright 2023-2024 the openage authors. See copying.md for legal info.

#include "controller.h"

#include <eigen3/Eigen/Dense>

#include "input/controller/hud/binding_context.h"

#include "renderer/stages/hud/render_entity.h"
#include "renderer/stages/hud/render_stage.h"


namespace openage::input::hud {

Controller::Controller() :
	drag_entity{nullptr} {}

bool Controller::process(const event_arguments &ev_args,
                         const std::shared_ptr<BindingContext> &ctx) {
	if (not ctx->is_bound(ev_args.e)) {
		return false;
	}

	auto bind = ctx->lookup(ev_args.e);
	bind.action(ev_args, this->shared_from_this());

	return true;
}

void Controller::set_drag_entity(const std::shared_ptr<renderer::hud::DragRenderEntity> &entity) {
	this->drag_entity = entity;
}

const std::shared_ptr<renderer::hud::DragRenderEntity> &Controller::get_drag_entity() const {
	return this->drag_entity;
}

void setup_defaults(const std::shared_ptr<BindingContext> &ctx,
                    const std::shared_ptr<renderer::hud::HudRenderStage> &hud_renderer) {
	binding_func_t drag_selection_init{[hud_renderer](const event_arguments &args,
	                                                  const std::shared_ptr<Controller> controller) {
		auto render_entity = std::make_shared<renderer::hud::DragRenderEntity>(args.mouse);
		if (hud_renderer) {
			hud_renderer->add_drag_entity(render_entity);
		}
		controller->set_drag_entity(render_entity);
	}};

	binding_action drag_selection_init_action{drag_selection_init};

	binding_func_t drag_selection_move{[](const event_arguments &args,
	                                      const std::shared_ptr<Controller> controller) {
		if (controller->get_drag_entity()) {
			controller->get_drag_entity()->update(args.mouse);
		}
	}};

	binding_action drag_selection_move_action{drag_selection_move};
	ctx->bind(event_class::MOUSE_MOVE, drag_selection_move_action);

	binding_func_t drag_selection_end{[hud_renderer](const event_arguments & /* args */,
	                                                 const std::shared_ptr<Controller> controller) {
		if (hud_renderer) {
			hud_renderer->remove_drag_entity();
		}
		controller->set_drag_entity(nullptr);
	}};

	binding_action drag_selection_end_action{drag_selection_end};

	auto bind_drag = [&](Qt::KeyboardModifiers modifiers) {
		const auto mod = static_cast<modset_t>(modifiers);
		ctx->bind({event_class::MOUSE_BUTTON, Qt::MouseButton::LeftButton, mod, QEvent::MouseButtonPress},
		          drag_selection_init_action);
		ctx->bind({event_class::MOUSE_BUTTON, Qt::MouseButton::LeftButton, mod, QEvent::MouseButtonRelease},
		          drag_selection_end_action);
	};

	for (auto modifiers : {Qt::KeyboardModifiers{},
	                       Qt::KeyboardModifiers{Qt::ShiftModifier},
	                       Qt::KeyboardModifiers{Qt::ControlModifier},
	                       Qt::ControlModifier | Qt::ShiftModifier,
	                       Qt::KeyboardModifiers{Qt::AltModifier}}) {
		bind_drag(modifiers);
	}
#ifdef __APPLE__
	bind_drag(Qt::MetaModifier);
	bind_drag(Qt::MetaModifier | Qt::ShiftModifier);
#endif
}

} // namespace openage::input::hud
