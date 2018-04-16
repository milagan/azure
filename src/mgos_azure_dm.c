/*
 * Copyright (c) 2014-2018 Cesanta Software Limited
 * All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the ""License"");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an ""AS IS"" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Direct Method support.
 * https://docs.microsoft.com/en-us/azure/iot-hub/iot-hub-devguide-direct-methods
 */

#include "mgos_azure.h"
#include "mgos_azure_internal.h"

#include <stdarg.h>
#include <stdlib.h>

#include "common/cs_dbg.h"

#include "frozen.h"
#include "mongoose.h"

#include "mgos_mqtt.h"

#define REQ_PREFIX "$iothub/methods/POST/"
#define REQ_PREFIX_LEN (sizeof(REQ_PREFIX) - 1)
#define RESP_PREFIX "$iothub/methods/res/"

static void mgos_azure_dm_ev(struct mg_connection *nc, const char *topic,
                             int topic_len, const char *msg, int msg_len,
                             void *ud) {
  int i;
  char idbuf[20] = {0};
  struct mg_str ts = {.p = topic, .len = topic_len};
  struct mgos_azure_dm_arg dma = {.payload = {.p = msg, .len = msg_len}};
  const char *mbegin, *mend, *rid, *tend = topic + topic_len;
  if (mg_strstr(ts, mg_mk_str_n(REQ_PREFIX, REQ_PREFIX_LEN)) != topic) {
    goto out;
  }
  mbegin = topic + REQ_PREFIX_LEN;
  for (mend = mbegin; mend < tend && *mend != '/'; mend++) {
  }
  dma.method = mg_mk_str_n(mbegin, mend - mbegin);
  if (dma.method.len == 0) goto out;
  rid = mg_strstr(ts, mg_mk_str("$rid="));
  if (rid == NULL) goto out;
  for (rid = rid + 5, i = 0; rid < tend && isxdigit((int) *rid); rid++, i++) {
    idbuf[i] = *rid;
  }
  dma.id = strtoll(idbuf, NULL, 16);
  LOG(LL_DEBUG, ("DM '%.*s' (%lld): '%.*s' '%.*s'", (int) dma.method.len,
                 dma.method.p, (long long int) dma.id, (int) dma.payload.len,
                 dma.payload.p, (int) ts.len, ts.p));
  mgos_event_trigger(MGOS_AZURE_EV_DM, &dma);

  return;

out:
  LOG(LL_ERROR, ("Invalid DM: '%.*s'", (int) ts.len, ts.p));
  (void) nc;
  (void) ud;
}

bool mgos_azure_dm_response(int64_t id, int status, const struct mg_str *resp) {
  bool res = false;
  char *topic = NULL;
  mg_asprintf(&topic, 0, RESP_PREFIX "%d/?$rid=%llx", status,
              (long long int) id);
  if (topic != NULL) {
    res = mgos_mqtt_pub(topic, resp->p, resp->len, 0 /* qos */,
                        false /* retain */);
    free(topic);
  }
  return res;
}

bool mgos_azure_dm_responsef(int64_t id, int status, const char *json_fmt,
                             ...) {
  bool res = false;
  va_list ap;
  char *resp;
  va_start(ap, json_fmt);
  resp = json_vasprintf(json_fmt, ap);
  va_end(ap);
  if (resp != NULL) {
    struct mg_str resp_s = mg_mk_str(resp);
    res = mgos_azure_dm_response(id, status, &resp_s);
    free(resp);
  }
  return res;
}

bool mgos_azure_dm_init(void) {
  if (!mgos_sys_config_get_azure_enable_dm()) return true;
  mgos_mqtt_sub(REQ_PREFIX "#", mgos_azure_dm_ev, NULL);
  return true;
}
