/* SPDX-License-Identifier: GPL-3.0-only */
/* Real session provisioning/cleanup and real HTTP ownership/TLS/allowlists.
 * Only the native PS4 HTTP ABI is replaced; its clock never sleeps in real
 * time and no request leaves this process. The established header fixture
 * is reused, rather than substituting the session logic under test. */
#define main profile_header_fixture_main
#define x4_module_symbol profile_header_fixture_symbol
#include "stream_os_profile_http_test.c"
#undef main
#undef x4_module_symbol

static uint64_t scenario_started, cancel_at;
static unsigned scenario, eta_calls, queue_seen, maximum_queue_seconds, eta_seen, last_eta;
static X4SessionSnapshot last_progress;
static bool eta_request[128];
static const char *eta_body;
static int eta_status;

static int32_t queue_request(int32_t conn, int32_t method, const char *url, uint64_t bytes)
{
    if (!strstr(url,"/v1/waittime/")) return native_request(conn,method,url,bytes);
    CHECK(!strcmp(url,"https://fixture-region.gssv-play-prod.xboxlive.com/v1/waittime/HOST_GAME"));
    CHECK(resource[conn].kind==TEST_CONNECTION && method==0 && !bytes);
    int32_t id=test_new_resource(TEST_REQUEST,conn);
    resource[id].method=method;resource[id].borrowed_url=url;
    resource[id].payload_size=bytes;resource[id].route=TEST_STATE;
    eta_request[id]=true;return id;
}
static int32_t queue_send(int32_t id,const void *body,size_t bytes)
{
    if(eta_request[id]){
        struct TestResource *r=&resource[id];
        CHECK(r->kind==TEST_REQUEST&&!body&&!bytes);
        CHECK(r->device_headers==1&&!strcmp(r->device,expected_native_device));
        CHECK(r->client_headers==1&&r->authorization_headers==1);
        r->response=eta_body;++eta_calls;return eta_status<0?eta_status:0;
    }
    int32_t rc=native_send(id,body,bytes);
    if(resource[id].route==TEST_STATE){
        uint64_t elapsed=(fake_clock-scenario_started)/1000000;
        if(scenario==0)resource[id].response=elapsed<240?"{\"state\":\"WaitingForResources\"}":
            elapsed<360?"{\"state\":\"Provisioning\"}":"{\"state\":\"ReadyToConnect\"}";
        else if(scenario==1)resource[id].response=elapsed<240?"{\"state\":\"WaitingForResources\"}":"{\"state\":\"Provisioning\"}";
        else if(scenario==2)resource[id].response="{\"state\":\"Provisioning\"}";
        else resource[id].response="{\"state\":\"WaitingForResources\"}";
    }
    return rc;
}
static int32_t queue_status(int32_t id,int32_t *status)
{
    if(eta_request[id]){*status=eta_status;return 0;}
    return native_status(id,status);
}
static int32_t queue_delete_template(int32_t id)
{
    int rc=native_delete_template(id);
    for(unsigned i=1;i<=next_resource;++i)CHECK(resource[i].kind==TEST_NONE);
    memset(eta_request,0,sizeof(eta_request));next_resource=0;return rc;
}
int x4_module_symbol(int handle,const char *name,void **address)
{
    if(!strcmp(name,"sceHttpCreateRequestWithURL")){*address=(void *)queue_request;return 0;}
    if(!strcmp(name,"sceHttpSendRequest")){*address=(void *)queue_send;return 0;}
    if(!strcmp(name,"sceHttpGetStatusCode")){*address=(void *)queue_status;return 0;}
    if(!strcmp(name,"sceHttpDeleteTemplate")){*address=(void *)queue_delete_template;return 0;}
    return profile_header_fixture_symbol(handle,name,address);
}
static void queue_progress(void *context,const X4SessionSnapshot *view)
{
    CHECK(!context);last_progress=*view;
    if(view->waiting_for_resources){
        ++queue_seen;
        if(view->queue_eta_available){CHECK(view->queue_eta_seconds<=300);
            if(eta_seen)CHECK(view->queue_eta_seconds<=last_eta);
            last_eta=view->queue_eta_seconds;++eta_seen;
        }
        if(view->queue_elapsed_seconds>maximum_queue_seconds)maximum_queue_seconds=view->queue_elapsed_seconds;
    }
    if(cancel_at&&fake_clock>=cancel_at)atomic_store(&cancel_flag,1);
}
static void one_queue_session(unsigned which,const char *estimate,int estimate_http)
{
    memset(resource,0,sizeof(resource));memset(route_calls,0,sizeof(route_calls));
    memset(&last_progress,0,sizeof(last_progress));memset(eta_request,0,sizeof(eta_request));
    next_resource=templates=terminated=public_device_headers=0;
    fake_clock=1000000;scenario_started=fake_clock;scenario=which;eta_calls=queue_seen=maximum_queue_seconds=eta_seen=0;last_eta=300;
    atomic_store(&cancel_flag,0);cancel_at=which==3?fake_clock+230000000:0;
    eta_body=estimate;eta_status=estimate_http;expected_contract();
    X4XboxWork *w=x4_xbox_work_new();CHECK(w);
#if X4_STREAM_PROFILE
    X4StreamRequestConfig config=x4_stream_request_config(X4_REQUEST_720P60,X4_COLOR_AUTO_709);
    CHECK(x4_xbox_work_set_request_config(w,&config));
#endif
    w->cancel=&cancel_flag;w->session_start=fake_clock;w->session_progress=queue_progress;
    int error=-1;w->http=x4_http_open(&error);CHECK(w->http&&!error);
    copy_text(w->origin,sizeof(w->origin),origin);
    copy_text(w->authorization,sizeof(w->authorization),"Bearer fixture-not-a-real-token");
    X4CatalogTitle title={0};copy_text(title.id,sizeof(title.id),"HOST_GAME");
    CHECK(session_create(w,&title)==1);
    /* The optional route must not broaden any destination/method policy. */
    static const char *invalid[]={"/v1/waittime/","/v1/waittime/HOST_GAME?x=1",
        "/v1/waittime/HOST_GAME/extra","/v1/waittime/..","/v1/waittime/%48OST_GAME",
        "/v1/waittime/HOST.GAME","/v1/waittime/HOST_GAME#fragment"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i){
        snprintf(w->url,sizeof(w->url),"%s%s",origin,invalid[i]);
        size_t length=1;int status=1;
        CHECK(session_call(w,X4_SESSION_HTTP_GET,NULL,w->cancel,&length,&status)==X4_HTTP_URL);
        CHECK(!length&&!status);
    }
    snprintf(w->url,sizeof(w->url),"%s/v1/waittime/HOST_GAME",origin);
    size_t denied_length=1;int denied_status=1;
    CHECK(session_call(w,X4_SESSION_HTTP_POST,"{}",w->cancel,&denied_length,&denied_status)==X4_HTTP_ARGUMENT);
    CHECK(session_call(w,X4_SESSION_HTTP_DELETE,NULL,w->cancel,&denied_length,&denied_status)==X4_HTTP_ARGUMENT);
    CHECK(session_call(w,X4_SESSION_HTTP_GET,"{}",w->cancel,&denied_length,&denied_status)==X4_HTTP_ARGUMENT);
    SessionEnd end={0};bool ready=session_provision(w,fake_clock+X4_SESSION_PROVISION_USEC,&end);
    uint64_t elapsed=(fake_clock-scenario_started)/1000000;
    if(which==0){CHECK(ready&&elapsed==360&&w->session.ready_seen);CHECK(w->session.queue_elapsed_seconds==240);end_with(&end,X4_SESSION_CLOSED,0,0,"fixture complete");}
    if(which==1){CHECK(!ready&&elapsed==420&&end.error==X4_AUTH_E_DEADLINE&&!w->session.queue_limit_reached);}
    if(which==2){CHECK(!ready&&elapsed==180&&end.error==X4_AUTH_E_DEADLINE&&!queue_seen&&!w->session.queue_limit_reached);}
    if(which==3){CHECK(!ready&&elapsed>=230&&elapsed<=231&&end.state==X4_SESSION_CANCELLED);}
    if(which==4){CHECK(!ready&&elapsed==900&&end.error==X4_AUTH_E_DEADLINE&&w->session.queue_limit_reached);}
    CHECK(!w->session.waiting_for_resources);
    bool valid_eta=!strcmp(estimate,"{\"estimatedTotalWaitTimeInSeconds\":300}")&&estimate_http==200&&which!=2;
    CHECK(valid_eta?(eta_seen>0):(eta_seen==0));
    if(which!=2){CHECK(queue_seen&&maximum_queue_seconds>=180);CHECK(eta_calls==1);}
    else CHECK(!eta_calls);
    session_cleanup(w,&end);
    CHECK(route_calls[TEST_DELETE]==1&&!w->session_id[0]&&!w->session.cleanup_failed);
    CHECK(last_progress.state==end.state&&last_progress.queue_elapsed_seconds==w->session.queue_elapsed_seconds);
    x4_http_close(w->http);w->http=NULL;
    CHECK(terminated==1);for(unsigned i=1;i<128;++i)CHECK(resource[i].kind==TEST_NONE);
    x4_xbox_work_free(w);
    printf("PASS queue scenario=%u elapsed_s=%llu eta_calls=%u snapshots=%u DELETE=1 HTTP resources=0\n",which,(unsigned long long)elapsed,eta_calls,queue_seen);
}
int main(void)
{
#if X4_STREAM_PROFILE
    runtime_profile=0;
#endif
    one_queue_session(0,"{\"estimatedTotalWaitTimeInSeconds\":300}",200);
    one_queue_session(1,"{}",200);
    one_queue_session(2,"{}",200);
    one_queue_session(3,"{}",503);
    one_queue_session(4,"{\"estimatedTotalWaitTimeInSeconds\":-1}",200);
    one_queue_session(0,"{\"estimatedTotalWaitTimeInSeconds\":\"300\"}",200);
    one_queue_session(0,"{\"estimatedTotalWaitTimeInSeconds\":1e309}",200);
    one_queue_session(0,"{\"estimatedTotalWaitTimeInSeconds\":86401}",200);
    one_queue_session(0,"{}",-7777);
    puts("PASS TAREA20c real HTTP: queue>180s, fresh180s provisioning, queue900s, cancel+DELETE, one optional ETA/fallback");
    return 0;
}
