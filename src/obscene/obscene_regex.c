
/* dist: public */

#include <stdio.h>
#include <string.h>
#include <pcre.h>

#include "asss.h"
#include "obscene.h"


local Imodman *mm;
local Iconfig *cfg;
local Ilogman *lm;
local Icmdman *cmd;
local Iplayerdata *pd;

local LinkedList obscene_words;
local int replace_count;
local pthread_mutex_t obscene_words_mtx = PTHREAD_MUTEX_INITIALIZER;

typedef struct
{
    pcre        *pattern;
    pcre_extra  *extra;
} re_data;

typedef struct {
  int overridden;         // Whether or not the obscene filter has been explicitly overridden by the player.
  int obscenity_filter;   // The player's selection. Ignored if overridden is false.
} PlayerObsceneData;

static int pdkey;

////////////////////////////////////////////////////////////////////////////////////////////////////

local void clear_obscene()
{
  re_data *regex;
  while( (regex = LLRemoveFirst(&obscene_words)) ) {
    pcre_free(regex->pattern);
    pcre_free(regex->extra);

    free(regex);
  }
}


local void load_obscene()
{
  // cfghelp: Chat:Obscene, global, string
  // A space-separated list of obscene words to filter.
  const char *words = cfg->GetStr(GLOBAL, "Chat", "Obscene");

  pthread_mutex_lock(&obscene_words_mtx);

  clear_obscene();

  if (words && strlen(words) >= 1)
  {
    char word[128];
    char expression[128];
    const char *tmp = NULL;
    const char *err;
    int err_offset;

    while(strsplit(words, " \t", word, sizeof(word), &tmp)) {
            re_data *regex = malloc(sizeof(re_data));

            // Build regex...
            sprintf(expression, "%s", word);

            regex->pattern = pcre_compile(expression, PCRE_CASELESS, &err, &err_offset, NULL);
            if(regex->pattern) {
                regex->extra = pcre_study(regex->pattern, 0, &err);
                LLAdd(&obscene_words, regex);

                if(err)
                    lm->Log(L_WARN, "<obscene> Pattern study error:  %s in \"%s\"", err, word);
            } else {
                lm->Log(L_WARN, "<obscene> Pattern compilation error:  %s in \"%s\"", err, word);
            }
    }

    lm->Log(L_INFO, "<obscene> loaded %d obscene words", LLCount(&obscene_words));
  }

  pthread_mutex_unlock(&obscene_words_mtx);
}


local int Filter(char *line) {
  static const char garbage[] = "%@$&%*!#@&%!#&*$#?@!*%@&!%#&%!?$*#!*$&@#&%$!*%@#&%!@&#$!*@&$%*@?#%@$&%*!#@&%!#&*$#?@!*%@&!%#&%!?$*#!";
  static const int glen = 100;

  int llen = strlen(line);
    int filtered = 0;

  pthread_mutex_lock(&obscene_words_mtx);
  for(Link *link = LLGetHead(&obscene_words); link; link = link->next) {
    re_data *regex = link->data;

    int cc; // capture count
    pcre_fullinfo(regex->pattern, regex->extra, PCRE_INFO_CAPTURECOUNT, &cc);

    int count = (cc + 1) * 3; // Number of elements, so we don't have to keep computing it.
                  // Note: The extra element is used as workspace by pcre.

    int offsets[count];     // cg + 0: Beginning of match (cg = capture group)
                  // cg + 1: End of match

    int err;
    int start = 0;

    while((err = pcre_exec(regex->pattern, regex->extra, line, llen, start, 0, offsets, count)) > 0) {
      filtered = 1;

      int mlen = offsets[1] - offsets[0];     // Match length

      int gpos = (replace_count++ << 2) % glen; // gpos = garbage position
      int grem = gpos + mlen - glen;        // grem = garbage remaining

      if(grem > 0) {
        int gend = glen - gpos;
        memcpy(line + offsets[0], garbage + gpos, gend);
        memcpy(line + offsets[0] + gend, garbage, grem);
      } else {
        memcpy(line + offsets[0], garbage + gpos, mlen);
      }

      start = offsets[1];
    }
  }

  pthread_mutex_unlock(&obscene_words_mtx);
  return filtered;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static void allocatePlayerCallback(Player *p, int allocating) {
  PlayerObsceneData *pdata = PPDATA(p, pdkey);

  pdata->overridden = 0;
  pdata->obscenity_filter = 0;
}


static void playerActionCallback(Player *player, int action, Arena *arena) {
  PlayerObsceneData *pdata;

  switch(action) {
    case PA_ENTERARENA:
      pdata = PPDATA(player, pdkey);

      if (pdata->overridden)
        player->flags.obscenity_filter = pdata->obscenity_filter;
  }
}

local helptext_t obscene_help =
"Targets: none\n"
"Args: none\n"
"Toggles the obscene word filter.\n";

local void Cobscene(const char *cmd, const char *params, Player *p, const Target *target)
{
  Ichat *chat = mm->GetInterface(I_CHAT, ALLARENAS);
  PlayerObsceneData *pdata = PPDATA(p, pdkey);

  pdata->overridden = 1;
  pdata->obscenity_filter = p->flags.obscenity_filter = !p->flags.obscenity_filter;

  chat->SendMessage(p, "Obscenity filter: %s", (p->flags.obscenity_filter ? "ON" : "OFF"));

  mm->ReleaseInterface(chat);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

local Iobscene obsceneint =
{
  INTERFACE_HEAD_INIT(I_OBSCENE, "obscene")
  Filter
};



static int GetInterfaces() {
  cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
  lm  = mm->GetInterface(I_LOGMAN, ALLARENAS);
  cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
  pd  = mm->GetInterface(I_PLAYERDATA, ALLARENAS);

  return (cfg && lm && cmd && pd);
}

static int ReleaseInterfaces() {
  mm->ReleaseInterface(pd);
  mm->ReleaseInterface(cmd);
  mm->ReleaseInterface(lm);
  mm->ReleaseInterface(cfg);

  return 1;
}

EXPORT int MM_obscene_regex(int action, Imodman *mm_, Arena *arena)
{
  switch(action) {
    case MM_LOAD:
      mm = mm_;

      if (!GetInterfaces()) {
        ReleaseInterfaces();
        return MM_FAIL;
      }

      mm->RegCallback(CB_GLOBALCONFIGCHANGED, load_obscene, ALLARENAS);
      mm->RegCallback(CB_NEWPLAYER, allocatePlayerCallback, ALLARENAS);
      mm->RegCallback(CB_PLAYERACTION, playerActionCallback, ALLARENAS);
      cmd->AddCommand("obscene", Cobscene, ALLARENAS, obscene_help);
      pdkey = pd->AllocatePlayerData(sizeof(PlayerObsceneData));

      LLInit(&obscene_words);
      replace_count = 0;
      load_obscene();

      mm->RegInterface(&obsceneint, ALLARENAS);

      return MM_OK;


    case MM_UNLOAD:
      if (mm->UnregInterface(&obsceneint, ALLARENAS))
        return MM_FAIL;

      pd->FreePlayerData(pdkey);
      cmd->RemoveCommand("obscene", Cobscene, ALLARENAS);
      mm->UnregCallback(CB_PLAYERACTION, playerActionCallback, ALLARENAS);
      mm->UnregCallback(CB_NEWPLAYER, allocatePlayerCallback, ALLARENAS);
      mm->UnregCallback(CB_GLOBALCONFIGCHANGED, load_obscene, ALLARENAS);

      pthread_mutex_lock(&obscene_words_mtx);
      clear_obscene();
      pthread_mutex_unlock(&obscene_words_mtx);

      ReleaseInterfaces();

      return MM_OK;
  }

  return MM_FAIL;
}

