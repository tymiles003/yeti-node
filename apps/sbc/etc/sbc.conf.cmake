#load_logic_module=lm_localfs
load_logic_module=yeti
# profiles - comma-separated list of call profiles to load
#
# <name>.sbcprofile.conf is loaded from module config 
# path (the path where this file resides)
#profiles=transparent,auth_b2b,sst_b2b

# active call profile - comma separated list, first non-empty is used
#
# o active_profile=<profile_name>  always use <profile_name>
#
# o active_profile=$(ruri.user)    use user part of INVITE Request URI
#
# o active_profile=$(paramhdr)     use  "profile" option in P-App-Param header
#
# o any replacement pattern
#
#active_profile=transparent

# regex_maps - comma-separated list of regex maps to load at startup, for $M()
# 
# regex=>value maps for which names are given here are loaded from 
# this path, e.g. src_ipmap.conf, ruri_map.conf, usermap.conf
#
#regex_maps=src_ipmap,ruri_map,usermap

# load_cc_plugins - semicolon-separated list of call-control plugins to load
#                   here the module names (.so names) must be specified, without .so
#                   analogous to load_plugins in sems.conf
#
# e.g. load_cc_plugins=cc_pcalls;cc_ctl
#load_cc_plugins=yeti
#load_cc_plugins=cc_pcalls;cc_ctl

## RFC4028 Session Timer
# default configuration - can be overridden by call profiles

# - enables the session timer ([yes,no]; default: no)
#
#enable_session_timer=yes

# - set the "Session-Expires" parameter for the session timer.
#
# session_expires=240

# - set the "Min-SE" parameter for the session timer.
#
# minimum_timer=90

# session refresh (Session Timer, RFC4028) method
#
# INVITE                 - use re-INVITE
# UPDATE                 - use UPDATE
# UPDATE_FALLBACK_INVITE - use UPDATE if indicated in Allow, re-INVITE otherwise
#
# Default: UPDATE_FALLBACK_INVITE
#
#session_refresh_method=UPDATE

# accept_501_reply - accept 501 reply as successful refresh? [yes|no]
#
# Default: yes
#
#accept_501_reply=no

core_options_handling=yes
