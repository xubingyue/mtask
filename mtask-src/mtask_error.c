#include "mtask.h"
#include "mtask_handle.h"
#include "mtask_mq.h"
#include "mtask_server.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define LOG_MESSAGE_SIZE 256

void 
mtask_error(struct mtask_context * context, const char *msg, ...) {
	static uint32_t logger = 0;
	if (logger == 0) {
		logger = mtask_handle_findname("logger");
	}
	if (logger == 0) {
		return;
	}

	char tmp[LOG_MESSAGE_SIZE];
	char *data = NULL;

	va_list ap;

	va_start(ap,msg);
	int len = vsnprintf(tmp, LOG_MESSAGE_SIZE, msg, ap);
	va_end(ap);
	if (len < LOG_MESSAGE_SIZE) {
		data = mtask_strdup(tmp);
	} else {
		int max_size = LOG_MESSAGE_SIZE;
		for (;;) {
			max_size *= 2;
			data = mtask_malloc(max_size);
			va_start(ap,msg);
			len = vsnprintf(data, max_size, msg, ap);
			va_end(ap);
			if (len < max_size) {
				break;
			}
			mtask_free(data);
		}
	}


	struct mtask_message smsg;
	if (context == NULL) {
		smsg.source = 0;
	} else {
		smsg.source = mtask_context_handle(context);
	}
	smsg.session = 0;
	smsg.data = data;
	smsg.sz = len | ((size_t)PTYPE_TEXT << MESSAGE_TYPE_SHIFT);
	mtask_context_push(logger, &smsg);
}

