/*
** Experimental s390x text helper dispatch.
** Copyright (C) 2005-2026 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_S390X_TEXT_H
#define _LJ_S390X_TEXT_H

#include "lj_obj.h"

#if LJ_TARGET_S390X
LJ_FUNC int lj_s390x_text_cmp_active(void);
LJ_FUNC int lj_s390x_text_find_active(void);
LJ_FUNC int lj_s390x_text_transform_active(void);
LJ_FUNC int lj_s390x_text_pattern_active(void);
LJ_FUNC int32_t LJ_FASTCALL lj_s390x_text_str_cmp(GCstr *a, GCstr *b);
LJ_FUNC const char *lj_s390x_text_find(const char *s, const char *p,
				       MSize slen, MSize plen);
LJ_FUNC char *lj_s390x_text_copy_reverse(char *w, const char *q, MSize len);
LJ_FUNC char *lj_s390x_text_copy_lower(char *w, const char *q, MSize len);
LJ_FUNC char *lj_s390x_text_copy_upper(char *w, const char *q, MSize len);
LJ_FUNC MSize lj_s390x_text_pattern_span(const char *s, const char *end, int cl);
LJ_FUNC MSize lj_s390x_text_pattern_seek(const char *s, const char *end, int cl);
#endif

#endif
