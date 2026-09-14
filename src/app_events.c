#include "app_events.h"

#include <errno.h>

K_MSGQ_DEFINE(app_events_queue, sizeof(struct app_event), 16, 4);

int app_events_publish(const struct app_event *event, k_timeout_t timeout)
{
	if (event == NULL) {
		return -EINVAL;
	}

	return k_msgq_put(&app_events_queue, event, timeout);
}

int app_events_receive(struct app_event *event, k_timeout_t timeout)
{
	if (event == NULL) {
		return -EINVAL;
	}

	return k_msgq_get(&app_events_queue, event, timeout);
}
