#include "mtask_log.h"
#include "mtask_timer.h"
#include "mtask.h"
#include "mtask_socket.h"
#include <string.h>
#include <time.h>

FILE * 
mtask_log_open(struct mtask_context * ctx, uint32_t handle) {
	const char * logpath = mtask_getenv("logpath");
	if (logpath == NULL)
		return NULL;
	size_t sz = strlen(logpath);
	char tmp[sz + 16];
	sprintf(tmp, "%s/%08x.log", logpath, handle);
	FILE *f = fopen(tmp, "ab");
	if (f) {
		uint32_t starttime = mtask_gettime_fixsec();
		uint32_t currenttime = mtask_gettime();
		time_t ti = starttime + currenttime/100;
		mtask_error(ctx, "Open log file %s", tmp);
		fprintf(f, "open time: %u %s", currenttime, ctime(&ti));
		fflush(f);
	} else {
		mtask_error(ctx, "Open log file %s fail", tmp);
	}
	return f;
}

void
mtask_log_close(struct mtask_context * ctx, FILE *f, uint32_t handle) {
	mtask_error(ctx, "Close log file :%08x", handle);
	fprintf(f, "close time: %u\n", mtask_gettime());
	fclose(f);
}

static void
log_blob(FILE *f, void * buffer, size_t sz) {
	size_t i;
	uint8_t * buf = buffer;
	for (i=0;i!=sz;i++) {
		fprintf(f, "%02x", buf[i]);
	}
}

static void
log_socket(FILE * f, struct mtask_socket_message * message, size_t sz) {
	fprintf(f, "[socket] %d %d %d ", message->type, message->id, message->ud);

	if (message->buffer == NULL) {
		const char *buffer = (const char *)(message + 1);
		sz -= sizeof(*message);
		const char * eol = memchr(buffer, '\0', sz);
		if (eol) {
			sz = eol - buffer;
		}
		fprintf(f, "[%*s]", (int)sz, (const char *)buffer);
	} else {
		sz = message->ud;
		log_blob(f, message->buffer, sz);
	}
	fprintf(f, "\n");
	fflush(f);
}

void 
mtask_log_output(FILE *f, uint32_t source, int type, int session, void * buffer, size_t sz) {
	if (type == PTYPE_SOCKET) {
		log_socket(f, buffer, sz);
	} else {
		uint32_t ti = mtask_gettime();
		fprintf(f, ":%08x %d %d %u ", source, type, session, ti);
		log_blob(f, buffer, sz);
		fprintf(f,"\n");
		fflush(f);
	}
}
