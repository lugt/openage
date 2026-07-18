// Copyright 2022-2023 the openage authors. See copying.md for legal info.

#include "clock.h"

#include <thread>

#include "log/log.h"


namespace openage::time {

Clock::Clock() :
	state{ClockState::INIT},
	max_tick_time{50},
	speed{1.0f},
	last_check{simclock_t::now()},
	start_time{simclock_t::now()},
	sim_time{0},
	sim_real_time{0} {
}

ClockState Clock::get_state() {
	std::shared_lock lock{this->mutex};
	return this->state;
}

void Clock::update_time() {
	if (this->state == ClockState::RUNNING) {
		// NOTE: the throttle sleep below must happen WITHOUT holding this->mutex.
		// TimeLoop::run() busy-loops update_time() with no other throttling, so the
		// interval between two calls is frequently <1ms and we hit the
		// `passed.count() == 0` branch. Sleeping while holding the write lock
		// starves every reader of this mutex: the presenter thread's
		// get_real_time() (called per render stage per frame) and the simulation
		// thread's get_time() both block on condition_variable::wait, which on
		// macOS's libc++ shared_mutex (write-preferring) lets the time loop
		// re-acquire the write lock before any reader is woken - so the game
		// freezes for the cumulative sleep time every frame. The same defect is
		// latent on Linux/glibc (whose shared_mutex lets in-flight readers finish
		// first, hiding the problem), so fixing it here is correct everywhere.
		bool throttle = false;
		{
			std::unique_lock lock{this->mutex};

			auto now = simclock_t::now();
			auto passed = std::chrono::duration_cast<std::chrono::milliseconds>(now - this->last_check);
			if (passed.count() == 0) {
				// prevent the clock from stalling the thread forever
				throttle = true;
			}
			else if (passed.count() > this->max_tick_time) {
				// if too much real time passes between two time updates, we only advance time by a small amount
				// this prevents the simulation from getting out of control during unplanned stops,
				// e.g. when debugging or if you close your laptop lid
				this->sim_time += this->speed * this->max_tick_time;
				this->sim_real_time += this->max_tick_time;
			}
			else {
				this->sim_time += this->speed * passed.count();
				this->sim_real_time += passed.count();
			}
			this->last_check = now;
			// TODO: Stop clock if it reaches 0.0s with negative speed?
		}
		if (throttle) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
}

time::time_t Clock::get_time() {
	std::shared_lock lock{this->mutex};

	// convert time unit from milliseconds to seconds
	return this->sim_time / 1000;
}

time::time_t Clock::get_real_time() {
	std::shared_lock lock{this->mutex};

	// convert time unit from milliseconds to seconds
	return this->sim_real_time / 1000;
}

speed_t Clock::get_speed() {
	std::shared_lock lock{this->mutex};
	return this->speed;
}

void Clock::set_speed(speed_t speed) {
	this->update_time();

	std::unique_lock lock{this->mutex};
	this->speed = speed;

	log::log(MSG(info) << "Clock speed set to " << this->speed);
}

void Clock::start() {
	std::unique_lock lock{this->mutex};

	auto now = simclock_t::now();
	this->start_time = now;
	this->last_check = now;
	this->state = ClockState::RUNNING;
}

void Clock::stop() {
	if (this->state == ClockState::RUNNING) [[likely]] {
		this->update_time();
	}

	std::unique_lock lock{this->mutex};
	this->state = ClockState::STOPPED;

	log::log(MSG(info) << "Clock stopped at "
	                   << this->sim_time << "ms (simulated) / "
	                   << this->sim_real_time << "ms (real)");
}

void Clock::pause() {
	if (this->state == ClockState::RUNNING) [[likely]] {
		this->update_time();
	}

	std::unique_lock lock{this->mutex};
	this->state = ClockState::PAUSED;

	log::log(MSG(info) << "Clock paused at "
	                   << this->sim_time << "ms (simulated) / "
	                   << this->sim_real_time << "ms (real)");
}

void Clock::resume() {
	std::unique_lock lock{this->mutex};

	if (this->state == ClockState::PAUSED) [[likely]] {
		this->last_check = simclock_t::now();
		this->state = ClockState::RUNNING;
	}

	log::log(MSG(info) << "Clock resumed at "
	                   << this->sim_time << "ms (simulated) / "
	                   << this->sim_real_time << "ms (real)");
}

} // namespace openage::time
