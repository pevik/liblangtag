/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * lt-lang.c
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
#include "lt-lang.h"
#include "lt-lang-private.h"


/**
 * SECTION: lt-lang
 * @Short_Description: A container class for Language subtag
 * @Title: Container - Language
 *
 * This container class provides a data access to Language subtag entry.
 */
struct _lt_lang_t {
	lt_mem_t  parent;
	gchar    *tag;
	gchar    *description;
	gchar    *suppress_script;
	gchar    *scope;
	gchar    *macrolanguage;
	gchar    *preferred_tag;
};

/*< private >*/

/*< protected >*/
lt_lang_t *
lt_lang_create(void)
{
	lt_lang_t *retval = lt_mem_alloc_object(sizeof (lt_lang_t));

	return retval;
}

void
lt_lang_set_name(lt_lang_t   *lang,
		 const gchar *description)
{
	g_return_if_fail (lang != NULL);
	g_return_if_fail (description != NULL);

	if (lang->description)
		lt_mem_remove_ref(&lang->parent, lang->description);
	lang->description = g_strdup(description);
	lt_mem_add_ref(&lang->parent, lang->description,
		       (lt_destroy_func_t)g_free);
}

void
lt_lang_set_tag(lt_lang_t   *lang,
		const gchar *subtag)
{
	g_return_if_fail (lang != NULL);
	g_return_if_fail (subtag != NULL);

	if (lang->tag)
		lt_mem_remove_ref(&lang->parent, lang->tag);
	lang->tag = g_strdup(subtag);
	lt_mem_add_ref(&lang->parent, lang->tag,
		       (lt_destroy_func_t)g_free);
}

void
lt_lang_set_preferred_tag(lt_lang_t   *lang,
			  const gchar *subtag)
{
	g_return_if_fail (lang != NULL);
	g_return_if_fail (subtag != NULL);

	if (lang->preferred_tag)
		lt_mem_remove_ref(&lang->parent, lang->preferred_tag);
	lang->preferred_tag = g_strdup(subtag);
	lt_mem_add_ref(&lang->parent, lang->preferred_tag,
		       (lt_destroy_func_t)g_free);
}

void
lt_lang_set_suppress_script(lt_lang_t   *lang,
			    const gchar *script)
{
	g_return_if_fail (lang != NULL);
	g_return_if_fail (script != NULL);

	if (lang->suppress_script)
		lt_mem_remove_ref(&lang->parent, lang->suppress_script);
	lang->suppress_script = g_strdup(script);
	lt_mem_add_ref(&lang->parent, lang->suppress_script,
		       (lt_destroy_func_t)g_free);
}

void
lt_lang_set_macro_language(lt_lang_t   *lang,
			   const gchar *macrolanguage)
{
	g_return_if_fail (lang != NULL);
	g_return_if_fail (macrolanguage != NULL);

	if (lang->macrolanguage)
		lt_mem_remove_ref(&lang->parent, lang->macrolanguage);
	lang->macrolanguage = g_strdup(macrolanguage);
	lt_mem_add_ref(&lang->parent, lang->macrolanguage,
		       (lt_destroy_func_t)g_free);
}

void
lt_lang_set_scope(lt_lang_t   *lang,
		  const gchar *scope)
{
	g_return_if_fail (lang != NULL);
	g_return_if_fail (scope != NULL);

	if (lang->scope)
		lt_mem_remove_ref(&lang->parent, lang->scope);
	lang->scope = g_strdup(scope);
	lt_mem_add_ref(&lang->parent, lang->scope,
		       (lt_destroy_func_t)g_free);
}

/*< public >*/
/**
 * lt_lang_ref:
 * @lang: a #lt_lang_t.
 *
 * Increases the reference count of @lang.
 *
 * Returns: (transfer none): the same @lang object.
 */
lt_lang_t *
lt_lang_ref(lt_lang_t *lang)
{
	g_return_val_if_fail (lang != NULL, NULL);

	return lt_mem_ref(&lang->parent);
}

/**
 * lt_lang_unref:
 * @lang: a #lt_lang_t.
 *
 * Decreases the reference count of @lang. when its reference count
 * drops to 0, the object is finalized (i.e. its memory is freed).
 */
void
lt_lang_unref(lt_lang_t *lang)
{
	if (lang)
		lt_mem_unref(&lang->parent);
}

/**
 * lt_lang_get_name:
 * @lang: a #lt_lang_t.
 *
 * Obtains the description of the subtag.
 *
 * Returns: a description string.
 */
const gchar *
lt_lang_get_name(const lt_lang_t *lang)
{
	g_return_val_if_fail (lang != NULL, NULL);

	return lang->description;
}

/**
 * lt_lang_get_better_tag:
 * @lang: a #lt_lang_t.
 *
 * Obtains the better tag for use. this is a convenient function to get
 * the preferred-value if available.
 *
 * Returns: a tag string.
 */
const gchar *
lt_lang_get_better_tag(const lt_lang_t *lang)
{
	const gchar *retval = lt_lang_get_preferred_tag(lang);

	if (!retval)
		retval = lt_lang_get_tag(lang);

	return retval;
}

/**
 * lt_lang_get_tag:
 * @lang: a #lt_lang_t.
 *
 * Obtains the tag name.
 *
 * Returns: a tag string.
 */
const gchar *
lt_lang_get_tag(const lt_lang_t *lang)
{
	g_return_val_if_fail (lang != NULL, NULL);

	return lang->tag;
}

/**
 * lt_lang_get_preferred_tag:
 * @lang: a #lt_lang_t.
 *
 * Obtains the preferred-value. this is available only when the tag is
 * marked as deprecated.
 *
 * Returns: a preferred-value for the tag or %NULL.
 */
const gchar *
lt_lang_get_preferred_tag(const lt_lang_t *lang)
{
	g_return_val_if_fail (lang != NULL, NULL);

	return lang->preferred_tag;
}

/**
 * lt_lang_get_suppress_script:
 * @lang: a #lt_lang_t.
 *
 * Obtains the suppress-script value. which shouldn't be used to form
 * language tags with the associated primary or extended language subtag.
 *
 * Returns: a suppress-script string or %NULL.
 */
const gchar *
lt_lang_get_suppress_script(const lt_lang_t *lang)
{
	g_return_val_if_fail (lang != NULL, NULL);

	return lang->suppress_script;
}

/**
 * lt_lang_get_macro_language:
 * @lang: a #lt_lang_t.
 *
 * Obtains the macrolanguage being assigned for the subtag.
 * This is available only when the subtag is registered as the macrolanguage
 * in ISO 639-3.
 *
 * Returns: a macrolanguage string or %NULL.
 */
const gchar *
lt_lang_get_macro_language(const lt_lang_t *lang)
{
	g_return_val_if_fail (lang != NULL, NULL);

	return lang->macrolanguage;
}

/**
 * lt_lang_get_scope:
 * @lang: a #lt_lang_t.
 *
 * Obtains the scope value indicating the type of language code according
 * to ISO 639.
 *
 * Returns: a scope string or %NULL.
 */
const gchar *
lt_lang_get_scope(const lt_lang_t *lang)
{
	g_return_val_if_fail (lang != NULL, NULL);

	return lang->scope;
}

/**
 * lt_lang_dump:
 * @lang: a #lt_lang_t.
 *
 * Dumps the container information to the standard output.
 */
void
lt_lang_dump(const lt_lang_t *lang)
{
	const gchar *preferred = lt_lang_get_preferred_tag(lang);
	const gchar *suppress = lt_lang_get_suppress_script(lang);
	const gchar *scope = lt_lang_get_scope(lang);
	const gchar *macrolang = lt_lang_get_macro_language(lang);
	GString *string = g_string_new(NULL);

	if (preferred) {
		if (string->len == 0)
			g_string_append(string, " (");
		g_string_append_printf(string, "preferred-value: %s",
				       preferred);
	}
	if (suppress) {
		if (string->len == 0)
			g_string_append(string, " (");
		else
			g_string_append(string, ", ");
		g_string_append_printf(string, "suppress-script: %s",
				       suppress);
	}
	if (scope) {
		if (string->len == 0)
			g_string_append(string, " (");
		else
			g_string_append(string, ", ");
		g_string_append_printf(string, "scope: %s",
				       scope);
	}
	if (macrolang) {
		if (string->len == 0)
			g_string_append(string, " (");
		else
			g_string_append(string, ", ");
		g_string_append_printf(string, "macrolanguage: %s",
				       macrolang);
	}
	if (string->len > 0)
		g_string_append(string, ")");

	g_print("Language: %s [%s]%s\n",
		lt_lang_get_tag(lang),
		lt_lang_get_name(lang),
		string->str);
	g_string_free(string, TRUE);
}

/**
 * lt_lang_compare:
 * @v1: a #lt_lang_t.
 * @v2: a #lt_lang_t.
 *
 * Compare if @v1 and @v2 is the same object or not.
 *
 * Returns: %TRUE if it's the same, otherwise %FALSE.
 */
gboolean
lt_lang_compare(const lt_lang_t *v1,
		const lt_lang_t *v2)
{
	const gchar *s1, *s2;

	if (v1 == v2)
		return TRUE;

	s1 = v1 ? lt_lang_get_tag(v1) : NULL;
	s2 = v2 ? lt_lang_get_tag(v2) : NULL;

	if (g_strcmp0(s1, "*") == 0 ||
	    g_strcmp0(s2, "*") == 0)
		return TRUE;

	return g_strcmp0(s1, s2) == 0;
}
