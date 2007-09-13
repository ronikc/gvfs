#include <config.h>

#include <glib/gi18n-lib.h>

#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>

#include "gcontenttypeprivate.h"

/* A content type is a platform specific string that defines the type
   of a file. On unix it is a mime type, on win32 it is an extension string
   like ".doc", ".txt" or a percieved string like "audio". Such strings
   can be looked up in the registry at HKEY_CLASSES_ROOT.
*/

#ifdef G_OS_WIN32

#include <windows.h>

static char *
get_registry_classes_key (const char *subdir,
			  const wchar_t *key_name)
{
  wchar_t *wc_key;
  HKEY reg_key = NULL;
  DWORD key_type;
  DWORD nbytes;
  char *value_utf8;

  value_utf8 = NULL;
  
  nbytes = 0;
  wc_key = g_utf8_to_utf16 (subdir, -1, NULL, NULL, NULL);
  if (RegOpenKeyExW (HKEY_CLASSES_ROOT, wc_key, 0,
		     KEY_QUERY_VALUE, &reg_key) == ERROR_SUCCESS &&
      RegQueryValueExW (reg_key, key_name, 0,
			&key_type, NULL, &nbytes) == ERROR_SUCCESS &&
      key_type == REG_SZ)
    {
      wchar_t *wc_temp = g_new (wchar_t, (nbytes+1)/2 + 1);
      RegQueryValueExW (reg_key, key_name, 0,
			&key_type, (LPBYTE) wc_temp, &nbytes);
      wc_temp[nbytes/2] = '\0';
      value_utf8 = g_utf16_to_utf8 (wc_temp, -1, NULL, NULL, NULL);
      g_free (wc_temp);
    }
  g_free (wc_key);
  
  if (reg_key != NULL)
    RegCloseKey (reg_key);

  return value_utf8;
}

gboolean
g_content_type_equals (const char *type1,
		       const char *type2)
{
  char *progid1, *progid2;
  gboolean res;
  
  g_return_val_if_fail (type1 != NULL, FALSE);
  g_return_val_if_fail (type2 != NULL, FALSE);

  if (strcmp (type1, type2) == 0)
    return TRUE;

  res = FALSE;
  progid1 = get_registry_classes_key (type1, NULL);
  progid2 = get_registry_classes_key (type2, NULL);
  if (progid1 != NULL && progid2 != NULL &&
      strcmp (progid1, progid2) == 0)
    res = TRUE;
  g_free (progid1);
  g_free (progid2);
  
  return res;
}

gboolean
g_content_type_is_a (const char   *type,
		     const char   *supertype)
{
  gboolean res;
  char *value_utf8;

  g_return_val_if_fail (type != NULL, FALSE);
  g_return_val_if_fail (supertype != NULL, FALSE);

  if (g_content_type_equals (type, supertype))
    return TRUE;

  res = FALSE;
  value_utf8 = get_registry_classes_key (type, L"PerceivedType");
  if (value_utf8 && strcmp (value_utf8, supertype) == 0)
    res = TRUE;
  g_free (value_utf8);
  
  return res;
}

gboolean
g_content_type_is_unknown (const char *type)
{
  return strcmp ("*", type) == 0;
}

char *
g_content_type_get_description (const char *type)
{
  char *progid;
  char *description;

  progid = get_registry_classes_key (type, NULL);
  if (progid)
    {
      description = get_registry_classes_key (progid, NULL);
      g_free (progid);

      if (description)
	return description;
    }

  if (g_content_type_is_unknown (type))
    return g_strdup (_("Unknown type"));
  return g_strdup_printf (_("%s filetype"), type);
}

char *
g_content_type_get_mime_type (const char   *type)
{
  char *mime;

  mime = get_registry_classes_key (type, L"Content Type");
  if (mime)
    return mime;
  else if (g_content_type_is_unknown (type))
    return g_strdup ("application/octet-stream");
  else if (*type == '.')
    return g_strdup_printf ("application/x-ext-%s", type+1);
  /* TODO: Map "image" to "image/ *", etc? */

  return g_strdup ("application/octet-stream");
}

char *
g_content_type_get_icon (const char   *type)
{
  /* TODO: How do we represent icons???
     In the registry they are the default value of
     HKEY_CLASSES_ROOT\<progid>\DefaultIcon with typical values like:
     <type>: <value>
     REG_EXPAND_SZ: %SystemRoot%\System32\Wscript.exe,3
     REG_SZ: shimgvw.dll,3
  */
  return NULL;
}

gboolean
g_content_type_can_be_executable (const char   *type)
{
  if (strcmp (type, ".exe") == 0 ||
      strcmp (type, ".com") == 0 ||
      strcmp (type, ".bat") == 0)
    return TRUE;
  return FALSE;
}

static gboolean
looks_like_text (const guchar *data, gsize data_size)
{
  gsize i;
  for (i = 0; i < data_size; i++)
    {
      if g_ascii_iscntrl (data[i])
	return FALSE;
    }
  return TRUE;
}

char *
g_content_type_guess (const char   *filename,
		      const guchar *data,
		      gsize         data_size)
{
  char *basename;
  char *type;
  char *dot;

  type = NULL;

  if (filename)
    {
      basename = g_path_get_basename (filename);
      dot = strrchr (basename, '.');
      if (dot)
	type = g_strdup (dot);
      g_free (basename);
    }

  if (type)
    return type;

  if (data && looks_like_text (data, data_size))
    return g_strdup (".txt");

  return g_strdup ("*");
}


GList *
g_get_registered_content_types (void)
{
  DWORD index;
  wchar_t keyname[256];
  DWORD key_len;
  char *key_utf8;
  GList *types;

  types = NULL;
  index = 0;
  key_len = 256;
  while (RegEnumKeyExW(HKEY_CLASSES_ROOT,
		       index,
		       keyname,
		       &key_len,
		       NULL,
		       NULL,
		       NULL,
		       NULL) == ERROR_SUCCESS)
    {
      key_utf8 = g_utf16_to_utf8 (keyname, -1, NULL, NULL, NULL);
      if (key_utf8)
	{
	  if (*key_utf8 == '.')
	    types = g_list_prepend (types, key_utf8);
	  else
	    g_free (key_utf8);
	}
      index++;
      key_len = 256;
    }
  
  return g_list_reverse (types);
}

#else /* !G_OS_WIN32 - Unix specific version */

#define XDG_PREFIX _gio_xdg
#include "xdgmime/xdgmime.h"

/* We lock this mutex whenever we modify global state in this module.  */
G_LOCK_DEFINE_STATIC (gio_xdgmime);

gsize
_g_unix_content_type_get_sniff_len (void)
{
  gsize size;

  G_LOCK (gio_xdgmime);
  size = xdg_mime_get_max_buffer_extents ();
  G_UNLOCK (gio_xdgmime);

  return size;
}

char *
_g_unix_content_type_unalias (const char *type)
{
  char *res;
  
  G_LOCK (gio_xdgmime);
  res = g_strdup (xdg_mime_unalias_mime_type (type));
  G_UNLOCK (gio_xdgmime);
  
  return res;
}

char **
_g_unix_content_type_get_parents (const char *type)
{
  const char *umime;
  const char **parents;
  GPtrArray *array;
  int i;

  array = g_ptr_array_new ();
  
  G_LOCK (gio_xdgmime);
  
  umime = xdg_mime_unalias_mime_type (type);
  g_ptr_array_add (array, g_strdup (umime));
  
  parents = xdg_mime_get_mime_parents (umime);
  for (i = 0; parents && parents[i] != NULL; i++)
    g_ptr_array_add (array, g_strdup (parents[i]));
  
  G_UNLOCK (gio_xdgmime);
  
  g_ptr_array_add (array, NULL);
  
  return (char **)g_ptr_array_free (array, FALSE);
}

gboolean
g_content_type_equals (const char   *type1,
		       const char   *type2)
{
  gboolean res;
  
  g_return_val_if_fail (type1 != NULL, FALSE);
  g_return_val_if_fail (type2 != NULL, FALSE);
  
  G_LOCK (gio_xdgmime);
  res = xdg_mime_mime_type_equal (type1, type2);
  G_UNLOCK (gio_xdgmime);
	
  return res;
}

gboolean
g_content_type_is_a (const char   *type,
		     const char   *supertype)
{
  gboolean res;
    
  g_return_val_if_fail (type != NULL, FALSE);
  g_return_val_if_fail (supertype != NULL, FALSE);
  
  G_LOCK (gio_xdgmime);
  res = xdg_mime_mime_type_subclass (type, supertype);
  G_UNLOCK (gio_xdgmime);
	
  return res;
}

gboolean
g_content_type_is_unknown (const char *type)
{
  return strcmp (XDG_MIME_TYPE_UNKNOWN, type) == 0;
}


typedef enum {
  MIME_TAG_TYPE_OTHER,
  MIME_TAG_TYPE_COMMENT,
} MimeTagType;

typedef struct {
  int current_type;
  int current_lang_level;
  int comment_lang_level;
  char *comment;
} MimeParser;


static int
language_level (const char *lang)
{
  const char * const *lang_list;
  int i;
  
  /* The returned list is sorted from most desirable to least
     desirable and always contains the default locale "C". */
  lang_list = g_get_language_names ();
  
  for (i = 0; lang_list[i]; i++)
    if (strcmp (lang_list[i], lang) == 0)
      return 1000-i;
  
  return 0;
}

static void
mime_info_start_element (GMarkupParseContext *context,
			 const gchar         *element_name,
			 const gchar        **attribute_names,
			 const gchar        **attribute_values,
			 gpointer             user_data,
			 GError             **error)
{
  int i;
  const char *lang;
  MimeParser *parser = user_data;
  
  if (strcmp (element_name, "comment") == 0)
    {
      lang = "C";
      for (i = 0; attribute_names[i]; i++)
	if (strcmp (attribute_names[i], "xml:lang") == 0)
	  {
	    lang = attribute_values[i];
	    break;
	  }
      
      parser->current_lang_level = language_level (lang);
      parser->current_type = MIME_TAG_TYPE_COMMENT;
    }
  else
    parser->current_type = MIME_TAG_TYPE_OTHER;
  
}

static void
mime_info_end_element (GMarkupParseContext *context,
		       const gchar         *element_name,
		       gpointer             user_data,
		       GError             **error)
{
  MimeParser *parser = user_data;
  
  parser->current_type = MIME_TAG_TYPE_OTHER;
}

static void
mime_info_text (GMarkupParseContext *context,
		const gchar         *text,
		gsize                text_len,  
		gpointer             user_data,
		GError             **error)
{
  MimeParser *parser = user_data;

  if (parser->current_type == MIME_TAG_TYPE_COMMENT &&
      parser->current_lang_level > parser->comment_lang_level)
    {
      g_free (parser->comment);
      parser->comment = g_strndup (text, text_len);
      parser->comment_lang_level = parser->current_lang_level;
    }
}

static char *
load_comment_for_mime_helper (const char *dir, const char *basename)
{
  GMarkupParseContext *context;
  char *filename, *data;
  gsize len;
  gboolean res;
  MimeParser parse_data = {0};
  GMarkupParser parser = {
    mime_info_start_element,
    mime_info_end_element,
    mime_info_text,
  };

  filename = g_build_filename (dir, "mime", basename, NULL);
  
  res = g_file_get_contents (filename,  &data,  &len,  NULL);
  g_free (filename);
  if (!res)
    return NULL;
  
  context = g_markup_parse_context_new   (&parser, 0, &parse_data, NULL);
  res = g_markup_parse_context_parse (context, data, len, NULL);
  g_free (data);
  g_markup_parse_context_free (context);
  
  if (!res)
    return NULL;

  return parse_data.comment;
}


static char *
load_comment_for_mime (const char *mimetype)
{
  const char * const* dirs;
  char *basename;
  char *comment;
  int i;

  basename = g_strdup_printf ("%s.xml", mimetype);

  comment = load_comment_for_mime_helper (g_get_user_data_dir (), basename);
  if (comment)
    {
      g_free (basename);
      return comment;
    }
  
  dirs = g_get_system_data_dirs ();

  for (i = 0; dirs[i] != NULL; i++)
    {
      comment = load_comment_for_mime_helper (dirs[i], basename);
      if (comment)
	{
	  g_free (basename);
	  return comment;
	}
    }
  g_free (basename);
  
  return g_strdup_printf (_("%s type"), mimetype);
}

char *
g_content_type_get_description (const char *type)
{
  static GHashTable *type_comment_cache = NULL;
  char *comment;
  
  G_LOCK (gio_xdgmime);
  if (type_comment_cache == NULL)
    type_comment_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  comment = g_hash_table_lookup (type_comment_cache, type);
  comment = g_strdup (comment);
  G_UNLOCK (gio_xdgmime);
  
  if (comment != NULL)
    return comment;

  comment = load_comment_for_mime (type);
  
  G_LOCK (gio_xdgmime);
  g_hash_table_insert (type_comment_cache,
		       g_strdup (type),
		       g_strdup (comment));
  G_UNLOCK (gio_xdgmime);

  return comment;
}

char *
g_content_type_get_mime_type (const char   *type)
{
  return g_strdup (type);
}

char *
g_content_type_get_icon (const char   *type)
{
  /* TODO: How do we represent icons??? */
  return NULL;
}

gboolean
g_content_type_can_be_executable (const char   *type)
{
  if (g_content_type_is_a (type, "application/x-executable")  ||
      g_content_type_is_a (type, "text/plain"))
    return TRUE;

  return FALSE;
}

static gboolean
looks_like_text (const guchar *data, gsize data_size)
{
  gsize i;
  for (i = 0; i < data_size; i++)
    {
      if g_ascii_iscntrl (data[i])
	return FALSE;
    }
  return TRUE;
}

char *
g_content_type_guess (const char   *filename,
		      const guchar *data,
		      gsize         data_size)
{
  char *basename;
  const char *name_mimetype, *sniffed_mimetype;
  char *mimetype;

  name_mimetype = NULL;
  sniffed_mimetype = NULL;
  
  G_LOCK (gio_xdgmime);
  
  if (filename)
    {
      basename = g_path_get_basename (filename);
      name_mimetype = xdg_mime_get_mime_type_from_file_name (basename);
      g_free (basename);
    }

  if (data)
    sniffed_mimetype = xdg_mime_get_mime_type_for_data (data, data_size);

  if (name_mimetype == NULL)
    {
      if (sniffed_mimetype == NULL)
	{
	  if (data && looks_like_text (data, data_size))
	    mimetype = g_strdup ("text/plain");
	  else
	    mimetype = g_strdup (XDG_MIME_TYPE_UNKNOWN);
	}
      else
	mimetype = g_strdup (sniffed_mimetype);
    }
  else
    {
      if (sniffed_mimetype == XDG_MIME_TYPE_UNKNOWN && data && looks_like_text (data, data_size))
	sniffed_mimetype = "text/plain";
      
      if (sniffed_mimetype == NULL || sniffed_mimetype == XDG_MIME_TYPE_UNKNOWN)
	mimetype = g_strdup (name_mimetype);
      else
	{
	  /* Both named and sniffed, use the sniffed type unless
	   * this is a special content type
	   */

	  /* Some container formats are often used in many types of files,
	     then look at name instead. */
	  if (name_mimetype != XDG_MIME_TYPE_UNKNOWN && 
	      ((strcmp (sniffed_mimetype, "application/x-ole-storage") == 0) ||
	       (strcmp (sniffed_mimetype, "text/xml") == 0) ||
	       (strcmp (sniffed_mimetype, "application/x-bzip") == 0) ||
	       (strcmp (sniffed_mimetype, "application/x-gzip") == 0) ||
	       (strcmp (sniffed_mimetype, "application/zip") == 0))) 
	    mimetype = g_strdup (name_mimetype);
	  /* If the name mimetype is a more specific version of the
	     sniffed type, use it. */
	  else if (name_mimetype != XDG_MIME_TYPE_UNKNOWN &&
		   xdg_mime_mime_type_subclass (name_mimetype, sniffed_mimetype))
	    mimetype = g_strdup (name_mimetype);
	  else
	    mimetype = g_strdup (sniffed_mimetype);
	}
    }
  
  G_UNLOCK (gio_xdgmime);

  return mimetype;
}

static gboolean
foreach_mimetype (gpointer  key,
		  gpointer  value,
		  gpointer  user_data)
{
  GList **l = user_data;

  *l = g_list_prepend (*l, (char *)key);
  return TRUE;
}

static void
enumerate_mimetypes_subdir (const char *dir, const char *prefix, GHashTable *mimetypes)
{
  DIR *d;
  struct dirent *ent;
  char *mimetype;

  d = opendir (dir);
  if (d)
    {
      while ((ent = readdir (d)) != NULL)
	{
	  if (g_str_has_suffix (ent->d_name, ".xml"))
	    {
	      mimetype = g_strdup_printf ("%s/%.*s", prefix, strlen (ent->d_name) - 4, ent->d_name);
	      g_hash_table_insert (mimetypes, mimetype, NULL);
	    }
	}
      closedir (d);
    }
}

static void
enumerate_mimetypes_dir (const char *dir, GHashTable *mimetypes)
{
  DIR *d;
  struct dirent *ent;
  char *mimedir;
  char *name;

  mimedir = g_build_filename (dir, "mime", NULL);
  
  d = opendir (mimedir);
  if (d)
    {
      while ((ent = readdir (d)) != NULL)
	{
	  if (strcmp (ent->d_name, "packages") != 0)
	    {
	      name = g_build_filename (mimedir, ent->d_name, NULL);
	      if (g_file_test (name, G_FILE_TEST_IS_DIR))
		enumerate_mimetypes_subdir (name, ent->d_name, mimetypes);
	      g_free (name);
	    }
	}
      closedir (d);
    }
  
  g_free (mimedir);
}

GList *
g_get_registered_content_types (void)
{
  const char * const* dirs;
  GHashTable *mimetypes;
  int i;
  GList *l;

  mimetypes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  enumerate_mimetypes_dir (g_get_user_data_dir (), mimetypes);
  dirs = g_get_system_data_dirs ();

  for (i = 0; dirs[i] != NULL; i++)
    enumerate_mimetypes_dir (dirs[i], mimetypes);

  l = NULL;
  g_hash_table_foreach_steal (mimetypes, foreach_mimetype, &l);
  g_hash_table_destroy (mimetypes);

  return l;
}

#endif /* Unix version */
