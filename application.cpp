#include <stdint.h>
#include <string.h>

#include <Wire.h>

#include "raat.hpp"
#include "raat-buffer.hpp"

#include "raat-oneshot-timer.hpp"
#include "raat-oneshot-task.hpp"
#include "raat-task.hpp"

#include "http-get-server.hpp"

#define MOVE_TIMEOUT_MS 15000UL

typedef enum _state
{
    eState_Idle,
    eState_Closing,
    eState_Opening
} eState;

static HTTPGetServer s_server(NULL);
static const raat_devices_struct * s_pDevices = NULL;

static eState s_state = eState_Idle;

static unsigned long s_move_timeout = 0U;

static void send_standard_erm_response()
{
    s_server.set_response_code_P(PSTR("200 OK"));
    s_server.set_header_P(PSTR("Access-Control-Allow-Origin"), PSTR("*"));
    s_server.finish_headers();
}

static void open_crate(char const * const url, char const * const end)
{
    (void)end;
    bool ok_to_open = s_pDevices->pLockSense->state() == false;

    if (url)
    {
        if (ok_to_open)
        {
            raat_logln_P(LOG_APP, PSTR("Opening crate (command)"));
        }
        else
        {
            raat_logln_P(LOG_APP, PSTR("Locked, cannot open"));
        }
        send_standard_erm_response();
    }

    if (ok_to_open)
    {
        s_pDevices->pRelay1->set(false);
        s_pDevices->pRelay2->set(true);
        s_state = eState_Opening;
        s_move_timeout = millis() + MOVE_TIMEOUT_MS;
    }
}

static void close_crate(char const * const url, char const * const end)
{
    (void)end;
    bool ok_to_close = s_pDevices->pLockSense->state() == false;

    if (ok_to_close)
    {    
        raat_logln_P(LOG_APP, PSTR("Closing crate"));
    }
    else
    {
        raat_logln_P(LOG_APP, PSTR("Locked, cannot close"));
    }

    if (url)
    {
        send_standard_erm_response();
    }

    if (ok_to_close)
    {
        s_pDevices->pRelay1->set(true);
        s_pDevices->pRelay2->set(false);
        s_state = eState_Closing;
        s_move_timeout = millis() + MOVE_TIMEOUT_MS;
    }
}

static void stop_crate(char const * const url, char const * const end)
{
    (void)url; (void)end;
    s_pDevices->pRelay1->set(false);
    s_pDevices->pRelay2->set(false);
    s_state = eState_Idle;
    s_move_timeout = (unsigned long)-1;
}

static void unlock_drawer(char const * const url, char const * const end)
{
    (void)end;
    raat_logln_P(LOG_APP, PSTR("Unlocking drawer"));
    if (url)
    {
        send_standard_erm_response();
    }
    s_pDevices->pRelay3->set(false);
}

static void lock_drawer(char const * const url, char const * const end)
{
    (void)end;
    raat_logln_P(LOG_APP, PSTR("Locking drawer"));
    if (url)
    {
        send_standard_erm_response();
    }
    s_pDevices->pRelay3->set(true);
}

static void set_spare(char const * const url, char const * const end)
{
    (void)end;
    raat_logln_P(LOG_APP, PSTR("Setting spare relay"));
    if (url)
    {
        send_standard_erm_response();
    }
    s_pDevices->pRelay4->set(true);
}

static void clear_spare(char const * const url, char const * const end)
{
    (void)end;
    raat_logln_P(LOG_APP, PSTR("Clearing spare relay"));
    if (url)
    {
        send_standard_erm_response();
    }
    s_pDevices->pRelay4->set(false);
}

static void get_reset_state(char const * const url, char const * const end)
{
    (void)end;
    bool b_reset_is_pressed = s_pDevices->pResetSense->state() == false;

    raat_logln_P(LOG_APP, PSTR("Reset state: %s"), b_reset_is_pressed ? "P" : "NP");
    if (url)
    {
        send_standard_erm_response();
    }
    s_server.add_body_P(b_reset_is_pressed ? PSTR("PRESSED\r\n\r\n") : PSTR("NOT PRESSED\r\n\r\n"));
}

static void get_lock_state(char const * const url, char const * const end)
{
    (void)end;
    bool b_locked = s_pDevices->pLockSense->state() == true;

    raat_logln_P(LOG_APP, PSTR("Lock state: %s"), b_locked ? "L" : "UL");
    if (url)
    {
        send_standard_erm_response();
    }
    s_server.add_body_P(b_locked ? PSTR("LOCKED\r\n\r\n") : PSTR("UNLOCKED\r\n\r\n"));
}

static const char OPEN_CRATE_URL[] PROGMEM = "/crate/open";
static const char CLOSE_CRATE_URL[] PROGMEM = "/crate/close";
static const char STOP_CRATE_URL[] PROGMEM = "/crate/stop";
static const char UNLOCK_DRAWER_URL[] PROGMEM = "/drawer/unlock";
static const char LOCK_DRAWER_URL[] PROGMEM = "/drawer/lock";
static const char SET_SPARE_URL[] PROGMEM = "/spare/set";
static const char CLEAR_SPARE_URL[] PROGMEM = "/spare/clear";
static const char RESET_GET_URL[] PROGMEM = "/reset/get";
static const char LOCK_GET_URL[] PROGMEM = "/lock/get";

static http_get_handler s_handlers[] = 
{
    {OPEN_CRATE_URL, open_crate},
    {CLOSE_CRATE_URL, close_crate},
    {STOP_CRATE_URL, stop_crate},
    {UNLOCK_DRAWER_URL, unlock_drawer},
    {LOCK_DRAWER_URL, lock_drawer},
    {SET_SPARE_URL, set_spare},
    {CLEAR_SPARE_URL, clear_spare},
    {RESET_GET_URL, get_reset_state},
    {LOCK_GET_URL, get_lock_state},
    {"", NULL}
};

void ethernet_packet_handler(char * req)
{
    s_server.handle_req(s_handlers, req);
}

char * ethernet_response_provider()
{
    return s_server.get_response();
}

void raat_custom_setup(const raat_devices_struct& devices, const raat_params_struct& params)
{
    (void)params;    
    s_pDevices = &devices;

    raat_logln_P(LOG_APP, PSTR("Cave Escape - Bomb Crate"));
}

void raat_custom_loop(const raat_devices_struct& devices, const raat_params_struct& params)
{
    (void)params;

    switch(s_state)
    {
    case eState_Idle:
        if (devices.pLockSense->check_low_and_clear())
        {
            raat_logln_P(LOG_APP, PSTR("Opening crate (button)"));
            open_crate(NULL, NULL);
        }
        break;
    case eState_Opening:
        if (devices.pLockSense->state() == true)
        {
            // Immediately stop any motion
            stop_crate(NULL, NULL);
        }
        if (s_move_timeout < millis())
        {
            raat_logln_P(LOG_APP, PSTR("Assumed open motion stop"));
            s_state = eState_Idle;   
        }
        break;
    case eState_Closing:
        if (devices.pLockSense->state() == true)
        {
            // Immediately stop any motion
            stop_crate(NULL, NULL);
        }
        if (s_move_timeout < millis())
        {
            raat_logln_P(LOG_APP, PSTR("Assumed close motion stop"));
            s_state = eState_Idle;   
        }
        break;
    }

    if (devices.pOverrideSense->check_low_and_clear())
    {
        if (devices.pLockSense->state() == false)
        {
            switch(s_state)
            {
            case eState_Idle:
            case eState_Opening:
                raat_logln_P(LOG_APP, PSTR("Closing crate (override)"));
                close_crate(NULL, NULL);
                break;
            case eState_Closing:
                raat_logln_P(LOG_APP, PSTR("Opening crate (override)"));
                open_crate(NULL, NULL);
                break;
            }
        }
    }
}
