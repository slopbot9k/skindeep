/* The Claude Code adapter (decisions 0009, 0021, 0030), as
 * app/metrics/claude/ has it. sk_provider_claude, in agent.h, makes the
 * part of a session the app starts. A session it did not start (decision
 * 0027) is followed by its transcript alone, through the watch part
 * below, as app/metrics/claude/watch.js does. */
#ifndef SK_CLAUDE_H
#define SK_CLAUDE_H

#include "../agent.h"

/* Names the transcript to follow: true with *file and *session
 * malloc'd, else false while there is none. Asked at the first poll,
 * then every 5 s, so a /clear, which starts a new transcript, is
 * followed. */
typedef bool (*sk_claude_find_fn)(void *ud, char **file, char **session);

/* A read-only part on the transcript find names, read as when neither
 * hooks nor a statusline were injected. Of ctx it takes bus, cmd (for
 * the compaction rule), log and now. ud outlives the part. */
sk_part sk_claude_watch(const sk_agent_ctx *ctx, sk_claude_find_fn find, void *ud);
/* The same with a ud the part owns, freed by free_ud when the part is,
 * such as one that keeps ctx->cmd for it. */
sk_part sk_claude_watch_owned(const sk_agent_ctx *ctx, sk_claude_find_fn find, void *ud,
                              void (*free_ud)(void *));

/* The settings files in store the JavaScript app wrote (decision 0036's
 * cutover): once its relay is gone, each hook and statusline command
 * that ran it runs relay instead, rewritten in the same file, so a
 * background session it started keeps its relays. While that relay is
 * there, or when relay cannot run, nothing is touched. The number of
 * files rewritten. */
int sk_claude_migrate_settings(const char *store, const char *relay);

#endif
