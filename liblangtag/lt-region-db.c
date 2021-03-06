/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * lt-region-db.c
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

#include <libxml/xpath.h>
#include "lt-error.h"
#include "lt-mem.h"
#include "lt-utils.h"
#include "lt-xml.h"
#include "lt-region.h"
#include "lt-region-private.h"
#include "lt-region-db.h"


/**
 * SECTION:lt-region-db
 * @Short_Description: An interface to access Region Database
 * @Title: Database - Region
 *
 * This class provides an interface to access Region database. which has been
 * registered as ISO 3166-1 and UN M.49 code.
 */
struct _lt_region_db_t {
	lt_mem_t    parent;
	lt_xml_t   *xml;
	GHashTable *region_entries;
};


/*< private >*/
static gboolean
lt_region_db_parse(lt_region_db_t  *regiondb,
		   GError         **error)
{
	gboolean retval = TRUE;
	xmlDocPtr doc = NULL;
	xmlXPathContextPtr xctxt = NULL;
	xmlXPathObjectPtr xobj = NULL;
	GError *err = NULL;
	int i, n;

	g_return_val_if_fail (regiondb != NULL, FALSE);

	doc = lt_xml_get_subtag_registry(regiondb->xml);
	xctxt = xmlXPathNewContext(doc);
	if (!xctxt) {
		g_set_error(&err, LT_ERROR, LT_ERR_OOM,
			    "Unable to create an instance of xmlXPathContextPtr.");
		goto bail;
	}
	xobj = xmlXPathEvalExpression((const xmlChar *)"/registry/region", xctxt);
	if (!xobj) {
		g_set_error(&err, LT_ERROR, LT_ERR_FAIL_ON_XML,
			    "No valid elements for %s",
			    doc->name);
		goto bail;
	}
	n = xmlXPathNodeSetGetLength(xobj->nodesetval);

	for (i = 0; i < n; i++) {
		xmlNodePtr ent = xmlXPathNodeSetItem(xobj->nodesetval, i);
		xmlNodePtr cnode;
		xmlChar *subtag = NULL, *desc = NULL, *preferred = NULL;
		lt_region_t *le = NULL;
		gchar *s;

		if (!ent) {
			g_set_error(&err, LT_ERROR, LT_ERR_FAIL_ON_XML,
				    "Unable to obtain the xml node via XPath.");
			goto bail;
		}
		cnode = ent->children;
		while (cnode != NULL) {
			if (xmlStrcmp(cnode->name, (const xmlChar *)"subtag") == 0) {
				if (subtag) {
					g_warning("Duplicate subtag element in region: previous value was '%s'",
						  subtag);
				} else {
					subtag = xmlNodeGetContent(cnode);
				}
			} else if (xmlStrcmp(cnode->name, (const xmlChar *)"added") == 0 ||
				   xmlStrcmp(cnode->name, (const xmlChar *)"text") == 0 ||
				   xmlStrcmp(cnode->name, (const xmlChar *)"deprecated") == 0 ||
				   xmlStrcmp(cnode->name, (const xmlChar *)"comments") == 0) {
				/* ignore it */
			} else if (xmlStrcmp(cnode->name, (const xmlChar *)"description") == 0) {
				/* wonder if many descriptions helps something. or is it a bug? */
				if (!desc)
					desc = xmlNodeGetContent(cnode);
			} else if (xmlStrcmp(cnode->name, (const xmlChar *)"preferred-value") == 0) {
				if (preferred) {
					g_warning("Duplicate preferred-value element in region: previous value was '%s'",
						  preferred);
				} else {
					preferred = xmlNodeGetContent(cnode);
				}
			} else {
				g_warning("Unknown node under /registry/region: %s", cnode->name);
			}
			cnode = cnode->next;
		}
		if (!subtag) {
			g_warning("No subtag node: description = '%s', preferred-value = '%s'",
				  desc, preferred);
			goto bail1;
		}
		if (!desc) {
			g_warning("No description node: subtag = '%s', preferred-value = '%s'",
				  subtag, preferred);
			goto bail1;
		}
		le = lt_region_create();
		if (!le) {
			g_set_error(&err, LT_ERROR, LT_ERR_OOM,
				    "Unable to create an instance of lt_region_t.");
			goto bail1;
		}
		lt_region_set_tag(le, (const gchar *)subtag);
		lt_region_set_name(le, (const gchar *)desc);
		if (preferred)
			lt_region_set_preferred_tag(le, (const gchar *)preferred);

		s = g_strdup(lt_region_get_tag(le));
		g_hash_table_replace(regiondb->region_entries,
				     lt_strlower(s),
				     lt_region_ref(le));
	  bail1:
		if (subtag)
			xmlFree(subtag);
		if (desc)
			xmlFree(desc);
		if (preferred)
			xmlFree(preferred);
		lt_region_unref(le);
	}
  bail:
	if (err) {
		if (error)
			*error = g_error_copy(err);
		else
			g_warning(err->message);
		g_error_free(err);
		retval = FALSE;
	}

	if (xobj)
		xmlXPathFreeObject(xobj);
	if (xctxt)
		xmlXPathFreeContext(xctxt);

	return retval;
}

/*< public >*/
/**
 * lt_region_db_new:
 *
 * Create a new instance of a #lt_region_db_t.
 *
 * Returns: (transfer full): a new instance of #lt_region_db_t.
 */
lt_region_db_t *
lt_region_db_new(void)
{
	lt_region_db_t *retval = lt_mem_alloc_object(sizeof (lt_region_db_t));

	if (retval) {
		GError *err = NULL;
		lt_region_t *le;

		retval->region_entries = g_hash_table_new_full(g_str_hash,
							       g_str_equal,
							       g_free,
							       (GDestroyNotify)lt_region_unref);
		lt_mem_add_ref(&retval->parent, retval->region_entries,
			       (lt_destroy_func_t)g_hash_table_destroy);

		le = lt_region_create();
		lt_region_set_tag(le, "*");
		lt_region_set_name(le, "Wildcard entry");
		g_hash_table_replace(retval->region_entries,
				     g_strdup(lt_region_get_tag(le)),
				     le);
		le = lt_region_create();
		lt_region_set_tag(le, "");
		lt_region_set_name(le, "Empty entry");
		g_hash_table_replace(retval->region_entries,
				     g_strdup(lt_region_get_tag(le)),
				     le);

		retval->xml = lt_xml_new();
		if (!retval->xml) {
			lt_region_db_unref(retval);
			retval = NULL;
			goto bail;
		}
		lt_mem_add_ref(&retval->parent, retval->xml,
			       (lt_destroy_func_t)lt_xml_unref);

		lt_region_db_parse(retval, &err);
		if (err) {
			g_printerr(err->message);
			lt_region_db_unref(retval);
			retval = NULL;
			g_error_free(err);
		}
	}
  bail:

	return retval;
}

/**
 * lt_region_db_ref:
 * @regiondb: a #lt_region_db_t.
 *
 * Increases the reference count of @regiondb.
 *
 * Returns: (transfer none): the same @regiondb object.
 */
lt_region_db_t *
lt_region_db_ref(lt_region_db_t *regiondb)
{
	g_return_val_if_fail (regiondb != NULL, NULL);

	return lt_mem_ref(&regiondb->parent);
}

/**
 * lt_region_db_unref:
 * @regiondb: a #lt_region_db_t.
 *
 * Decreases the reference count of @regiondb. when its reference count
 * drops to 0, the object is finalized (i.e. its memory is freed).
 */
void
lt_region_db_unref(lt_region_db_t *regiondb)
{
	if (regiondb)
		lt_mem_unref(&regiondb->parent);
}

/**
 * lt_region_db_lookup:
 * @regiondb: a #lt_region_db_t.
 * @language_or_code: a region code to lookup.
 *
 * Lookup @lt_region_t if @language_or_code is valid and registered into
 * the database.
 *
 * Returns: (transfer full): a #lt_region_t that meets with @language_or_code.
 *                           otherwise %NULL.
 */
lt_region_t *
lt_region_db_lookup(lt_region_db_t *regiondb,
		    const gchar    *language_or_code)
{
	lt_region_t *retval;
	gchar *s;

	g_return_val_if_fail (regiondb != NULL, NULL);
	g_return_val_if_fail (language_or_code != NULL, NULL);

	s = g_strdup(language_or_code);
	retval = g_hash_table_lookup(regiondb->region_entries,
				     lt_strlower(s));
	g_free(s);
	if (retval)
		return lt_region_ref(retval);

	return NULL;
}
