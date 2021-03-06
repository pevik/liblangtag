/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * lt-extlang-db.h
 * Copyright (C) 2011-2012 Akira TAGOH
 * 
 * Authors:
 *   Akira TAGOH  <akira@tagoh.org>
 * 
 * This library is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#if !defined (__LANGTAG_H__INSIDE) && !defined (__LANGTAG_COMPILATION)
#error "Only <liblangtag/langtag.h> can be included directly."
#endif

#ifndef __LT_EXTLANG_DB_H__
#define __LT_EXTLANG_DB_H__

#include <glib.h>
#include <liblangtag/lt-extlang.h>

G_BEGIN_DECLS

/**
 * lt_extlang_db_t:
 *
 * All the fields in the <structname>lt_extlang_db_t</structname>
 * structure are private to the #lt_extlang_db_t implementation.
 */
typedef struct _lt_extlang_db_t	lt_extlang_db_t;


lt_extlang_db_t *lt_extlang_db_new   (void);
lt_extlang_db_t *lt_extlang_db_ref   (lt_extlang_db_t *extlangdb);
void             lt_extlang_db_unref (lt_extlang_db_t *extlangdb);
lt_extlang_t    *lt_extlang_db_lookup(lt_extlang_db_t *extlangdb,
                                      const gchar     *subtag);

G_END_DECLS

#endif /* __LT_EXTLANG_DB_H__ */
