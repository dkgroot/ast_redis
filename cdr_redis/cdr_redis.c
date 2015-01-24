#
#
#

#include <asterisk.h>

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 419592 $")

#include <hiredis/hiredis.h>
#include <hiredis/async.h>

#include <asterisk/config.h>
#include <asterisk/channel.h>
#include <asterisk/cdr.h>
#include <asterisk/cli.h>
#include <asterisk/module.h>

