/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * lt-extlang.c
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lt-mem.h"
#include "lt-extlang.h"
#include "lt-extlang-private.h"


struct _lt_extlang_t {
	lt_mem_t  parent;
	gchar    *tag;
	gchar    *description;
	gchar    *macrolanguage;
};

/*< private >*/

/*< protected >*/
lt_extlang_t *
lt_extlang_create(void)
{
	lt_extlang_t *retval;

	retval = lt_mem_alloc_object(sizeof (lt_extlang_t));

	return retval;
}

void
lt_extlang_set_tag(lt_extlang_t *extlang,
		   const gchar  *subtag)
{
	g_return_if_fail (extlang != NULL);
	g_return_if_fail (subtag != NULL);

	if (extlang->tag)
		lt_mem_remove_ref(&extlang->parent, extlang->tag);
	extlang->tag = g_strdup(subtag);
	lt_mem_add_ref(&extlang->parent, extlang->tag,
		       (lt_destroy_func_t)g_free);
}

void
lt_extlang_set_name(lt_extlang_t *extlang,
		    const gchar  *description)
{
	g_return_if_fail (extlang != NULL);
	g_return_if_fail (description != NULL);

	if (extlang->description)
		lt_mem_remove_ref(&extlang->parent, extlang->description);
	extlang->description = g_strdup(description);
	lt_mem_add_ref(&extlang->parent, extlang->description,
		       (lt_destroy_func_t)g_free);
}

void
lt_extlang_set_macro_language(lt_extlang_t *extlang,
			      const gchar  *macrolanguage)
{
	g_return_if_fail (extlang != NULL);
	g_return_if_fail (macrolanguage != NULL);

	if (extlang->macrolanguage)
		lt_mem_remove_ref(&extlang->parent, extlang->macrolanguage);
	extlang->macrolanguage = g_strdup(macrolanguage);
	lt_mem_add_ref(&extlang->parent, extlang->macrolanguage,
		       (lt_destroy_func_t)g_free);
}

/*< public >*/
lt_extlang_t *
lt_extlang_ref(lt_extlang_t *extlang)
{
	g_return_val_if_fail (extlang != NULL, NULL);

	return lt_mem_ref(&extlang->parent);
}

void
lt_extlang_unref(lt_extlang_t *extlang)
{
	if (extlang)
		lt_mem_unref(&extlang->parent);
}

const gchar *
lt_extlang_get_tag(const lt_extlang_t *extlang)
{
	g_return_val_if_fail (extlang != NULL, NULL);

	return extlang->tag;
}

const gchar *
lt_extlang_get_name(const lt_extlang_t *extlang)
{
	g_return_val_if_fail (extlang != NULL, NULL);

	return extlang->description;
}

const gchar *
lt_extlang_get_macro_language(const lt_extlang_t *extlang)
{
	g_return_val_if_fail (extlang != NULL, NULL);

	return extlang->macrolanguage;
}
