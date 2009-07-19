/* $OpenBSD: cmd-kill-pane.c,v 1.4 2009/07/19 13:21:40 nicm Exp $ */

/*
 * Copyright (c) 2009 Nicholas Marriott <nicm@users.sourceforge.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <stdlib.h>

#include "tmux.h"

/*
 * Kill pane.
 */

int	cmd_kill_pane_exec(struct cmd *, struct cmd_ctx *);

const struct cmd_entry cmd_kill_pane_entry = {
	"kill-pane", "killp",
	CMD_PANE_WINDOW_USAGE,
	0, 0,
	cmd_pane_init,
	cmd_pane_parse,
	cmd_kill_pane_exec,
       	cmd_pane_send,
	cmd_pane_recv,
	cmd_pane_free,
	cmd_pane_print
};

int
cmd_kill_pane_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct cmd_pane_data	*data = self->data;
	struct winlink		*wl;
	struct window_pane	*wp;

	if ((wl = cmd_find_window(ctx, data->target, NULL)) == NULL)
		return (-1);
	if (data->pane == -1)
		wp = wl->window->active;
	else {
		wp = window_pane_at_index(wl->window, data->pane);
		if (wp == NULL) {
			ctx->error(ctx, "no pane: %d", data->pane);
			return (-1);
		}
	}

	if (window_count_panes(wl->window) == 1) {
		/* Only one pane, kill the window. */
		server_kill_window(wl->window);
		return (0);
	}

	layout_close_pane(wp);
	window_remove_pane(wl->window, wp);
	server_redraw_window(wl->window);
	return (0);
}
