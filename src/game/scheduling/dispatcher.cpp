/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019-2022 OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "pch.hpp"

#include "game/scheduling/dispatcher.hpp"
#include "lib/thread/thread_pool.hpp"
#include "lib/di/container.hpp"

Dispatcher &Dispatcher::getInstance() {
	return inject<Dispatcher>();
}

void Dispatcher::init() {
	Task::TIME_NOW = std::chrono::system_clock::now();

	threadPool.addLoad([this] {
		std::unique_lock asyncLock(mutex);

		while (!threadPool.getIoContext().stopped()) {
			// Current Time Cache
			Task::TIME_NOW = std::chrono::system_clock::now();

			// Execute all asynchronous events separately by context
			for (auto &context : asyncEventTasks) {
				const auto sizeEventAsync = context.size();
				std::atomic_uint_fast64_t executedTasks = 0;

				// Execute Async Task
				for (auto &task : context) {
					threadPool.addLoad([&] {
						task.execute();
						++executedTasks;
						task_async_signal.notify_one();
					});
				}

				// Wait for all the tasks in the current context to be executed.
				task_async_signal.wait(asyncLock, [&] {
					return executedTasks == sizeEventAsync;
				});

				// Clear all tasks
				context.clear();
			}

			// Merge all events that were created by async events
			merge_events();

			execute_events();
			execute_scheduled_events();

			// Merge all events that were created by Events and Scheduled Events
			merge_events();

			std::this_thread::sleep_for(std::chrono::milliseconds(15));
		}
	});
}

void Dispatcher::addEvent(std::function<void(void)> &&f, std::string &&context, uint32_t expiresAfterMs) {
	threads[getThreadId()].tasks.emplace_back(expiresAfterMs, f, context);
}

void Dispatcher::addEvent_async(std::function<void(void)> &&f, AsyncEventContext context) {
	threads[getThreadId()].asyncTasks[static_cast<uint8_t>(context)].emplace_back(0, f, "Dispatcher::addEvent_async");
}

uint64_t Dispatcher::scheduleEvent(const std::shared_ptr<Task> &task) {
	threads[getThreadId()].scheduledtasks.emplace_back(task);
	return scheduledtasksRef.emplace(task->generateId(), task).first->first;
}

uint64_t Dispatcher::scheduleEvent(uint32_t delay, std::function<void(void)> &&f, std::string &&context, bool cycle) {
	const auto &task = std::make_shared<Task>(std::move(f), std::move(context), delay, cycle);
	return scheduleEvent(task);
}

void Dispatcher::stopEvent(uint64_t eventId) {
	auto it = scheduledtasksRef.find(eventId);
	if (it == scheduledtasksRef.end()) {
		return;
	}

	it->second->cancel();
	scheduledtasksRef.erase(it);
}

void Dispatcher::execute_events() {
	for (auto &task : eventTasks) {
		if (task.execute()) {
			++dispatcherCycle;
		}
	}
	eventTasks.clear();
}

void Dispatcher::execute_scheduled_events() {
	for (uint_fast64_t i = 0, max = scheduledtasks.size(); i < max && !scheduledtasks.empty(); ++i) {
		const auto &task = scheduledtasks.top();
		if (task->getTime() > Task::TIME_NOW) {
			break;
		}

		task->execute();

		if (!task->isCanceled() && task->isCycle()) {
			scheduledtasks.emplace(task);
		} else {
			scheduledtasksRef.erase(task->getEventId());
		}

		scheduledtasks.pop();
	}
}

// Merge thread events with main dispatch events
void Dispatcher::merge_events() {
	for (auto &thread : threads) {
		if (!thread.tasks.empty()) {
			eventTasks.insert(eventTasks.end(), make_move_iterator(thread.tasks.begin()), make_move_iterator(thread.tasks.end()));
			thread.tasks.clear();
		}

		for (uint_fast8_t i = 0; i < static_cast<uint8_t>(AsyncEventContext::LAST); ++i) {
			auto &context = thread.asyncTasks[i];
			if (!context.empty()) {
				asyncEventTasks[i].insert(asyncEventTasks[i].end(), make_move_iterator(context.begin()), make_move_iterator(context.end()));
				context.clear();
			}
		}

		if (!thread.scheduledtasks.empty()) {
			for (auto &task : thread.scheduledtasks) {
				scheduledtasks.emplace(task);
				scheduledtasksRef.emplace(task->getEventId(), task);
			}
			thread.scheduledtasks.clear();
		}
	}
}
