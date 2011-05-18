/*
 *
 *   auth_mellon_handler.c: an authentication apache module
 *   Copyright � 2003-2007 UNINETT (http://www.uninett.no/)
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 * 
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */


#include "auth_mellon.h"


#ifdef HAVE_lasso_server_new_from_buffers
#  define SERVER_NEW lasso_server_new_from_buffers
#else /* HAVE_lasso_server_new_from_buffers */
#  define SERVER_NEW lasso_server_new
#endif /* HAVE_lasso_server_new_from_buffers */



#ifdef HAVE_lasso_server_new_from_buffers
/* This function generates optional metadata for a given element
 *
 * Parameters:
 *  apr_pool_t *p        Pool to allocate memory from
 *  apr_hash_t *t        Hash of lang -> strings
 *  const char *e        Name of the element
 *
 * Returns:
 *  the metadata, or NULL if an error occured
 */
static char *am_optional_metadata_element(apr_pool_t *p,
                                          apr_hash_t *h,
                                          const char *e)
{
    apr_hash_index_t *index;
    char *data = "";

    for (index = apr_hash_first(p, h); index; index = apr_hash_next(index)) {
        char *lang;
        char *value;
        apr_ssize_t slen;
	char *xmllang = "";

        apr_hash_this(index, (const void **)&lang, &slen, (void *)&value);
        
        if (*lang != '\0')
            xmllang = apr_psprintf(p, " xml:lang=\"%s\"", lang);

        data = apr_psprintf(p, "%s<%s%s>%s</%s>",
                            data, e, xmllang, value, e);
    }

    return data;
}

/* This function generates optinal metadata
 *
 * Parameters:
 *  request_rec *r       The request we received.
 *
 * Returns:
 *  the metadata, or NULL if an error occured
 */
static char *am_optional_metadata(apr_pool_t *p, request_rec *r)
{
    am_dir_cfg_rec *cfg = am_get_dir_cfg(r);
    int count = 0;
    char *org_data = NULL;
    char *org_name = NULL;
    char *org_display_name = NULL;
    char *org_url = NULL;

    count += apr_hash_count(cfg->sp_org_name);
    count += apr_hash_count(cfg->sp_org_display_name);
    count += apr_hash_count(cfg->sp_org_url);

    if (count == 0) 
        return "";

    org_name = am_optional_metadata_element(p, cfg->sp_org_name,
                                            "OrganizationName");
    org_display_name = am_optional_metadata_element(p, cfg->sp_org_display_name,
                                                    "OrganizationDisplayName");
    org_url = am_optional_metadata_element(p, cfg->sp_org_url,
                                           "OrganizationURL");
    org_data = apr_psprintf(p, "<Organization>%s%s%s</Organization>",
                            org_name, org_display_name, org_url);

    return org_data;
}


/* This function generates metadata
 *
 * Parameters:
 *  request_rec *r       The request we received.
 *
 * Returns:
 *  the metadata, or NULL if an error occured
 */
static char *am_generate_metadata(apr_pool_t *p, request_rec *r)
{
    am_dir_cfg_rec *cfg = am_get_dir_cfg(r);
    char *url = am_get_endpoint_url(r);
    char *cert = "";

    if (cfg->sp_cert_file) {
	char *sp_cert_file;
        char *cp;
        char *bp;
        const char *begin = "-----BEGIN CERTIFICATE-----";
        const char *end = "-----END CERTIFICATE-----";

        /* 
         * Try to remove leading and trailing garbage, as it can
         * wreak havoc XML parser if it contains [<>&]
         */
	sp_cert_file = apr_pstrdup(p, cfg->sp_cert_file);

        cp = strstr(sp_cert_file, begin);
        if (cp != NULL) 
            sp_cert_file = cp + strlen(begin);

        cp = strstr(sp_cert_file, end);
        if (cp != NULL)
            *cp = '\0';
        
	/* 
	 * And remove any non printing char (CR, spaces...)
	 */
	bp = sp_cert_file;
	for (cp = sp_cert_file; *cp; cp++) {
		if (apr_isgraph(*cp))
			*bp++ = *cp;
	}
	*bp = '\0';

        cert = apr_psprintf(p,
          "<KeyDescriptor use=\"signing\">"
            "<ds:KeyInfo xmlns:ds=\"http://www.w3.org/2000/09/xmldsig#\">"
              "<ds:X509Data>"
                "<ds:X509Certificate>%s</ds:X509Certificate>"
              "</ds:X509Data>"
            "</ds:KeyInfo>"
          "</KeyDescriptor>"
          "<KeyDescriptor use=\"encryption\">"
            "<ds:KeyInfo xmlns:ds=\"http://www.w3.org/2000/09/xmldsig#\">"
              "<ds:X509Data>"
                "<ds:X509Certificate>%s</ds:X509Certificate>"
              "</ds:X509Data>"
            "</ds:KeyInfo>"
          "</KeyDescriptor>",
          sp_cert_file,
          sp_cert_file);
    }

    return apr_psprintf(p,
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n\
<EntityDescriptor\n\
 entityID=\"%smetadata\"\n\
 xmlns=\"urn:oasis:names:tc:SAML:2.0:metadata\">\n\
 <SPSSODescriptor\n\
   AuthnRequestsSigned=\"true\"\n\
   WantAssertionsSigned=\"true\"\n\
   protocolSupportEnumeration=\"urn:oasis:names:tc:SAML:2.0:protocol\">\n\
   %s\
   <SingleLogoutService\n\
     Binding=\"urn:oasis:names:tc:SAML:2.0:bindings:SOAP\"\n\
     Location=\"%slogout\" />\n\
   <SingleLogoutService\n\
     Binding=\"urn:oasis:names:tc:SAML:2.0:bindings:HTTP-Redirect\"\n\
     Location=\"%slogout\" />\n\
   <NameIDFormat>urn:oasis:names:tc:SAML:2.0:nameid-format:transient</NameIDFormat>\n\
   <AssertionConsumerService\n\
     index=\"0\"\n\
     isDefault=\"true\"\n\
     Binding=\"urn:oasis:names:tc:SAML:2.0:bindings:HTTP-POST\"\n\
     Location=\"%spostResponse\" />\n\
   <AssertionConsumerService\n\
     index=\"1\"\n\
     Binding=\"urn:oasis:names:tc:SAML:2.0:bindings:HTTP-Artifact\"\n\
     Location=\"%sartifactResponse\" />\n\
 </SPSSODescriptor>\n\
 %s\n\
</EntityDescriptor>",
      url, cert, url, url, url, url, am_optional_metadata(p, r));
}
#endif /* HAVE_lasso_server_new_from_buffers */


/*
 * This function loads all IdP metadata in a lasso server
 *
 * Parameters:
 *  request_rec *r       The request we received.
 *
 * Returns:
 *  number of loaded providers
 */
static guint am_server_add_providers(request_rec *r)
{
    am_dir_cfg_rec *cfg = am_get_dir_cfg(r);
    const char *idp_metadata_file;
    const char *idp_public_key_file;
    apr_size_t index;

    if (cfg->idp_metadata_files->nelts == 1)
        idp_public_key_file = cfg->idp_public_key_file;
    else
        idp_public_key_file = NULL;

    for (index = 0; index < cfg->idp_metadata_files->nelts; index++) {
        int ret;
        idp_metadata_file = APR_ARRAY_IDX(cfg->idp_metadata_files, index,
                                          const char *);

	ret = lasso_server_add_provider(cfg->server, LASSO_PROVIDER_ROLE_IDP,
					idp_metadata_file,
					idp_public_key_file,
					cfg->idp_ca_file);
	if (ret != 0) {
	    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "Error adding IdP from \"%s\" to lasso server object.",
                          idp_metadata_file);
        }
    }

    return g_hash_table_size(cfg->server->providers);
}

static LassoServer *am_get_lasso_server(request_rec *r)
{
    am_dir_cfg_rec *cfg = am_get_dir_cfg(r);

    apr_thread_mutex_lock(cfg->server_mutex);
    if(cfg->server == NULL) {
        if(cfg->sp_metadata_file == NULL) {

#ifdef HAVE_lasso_server_new_from_buffers
            /*
             * Try to generate missing metadata
             */
            apr_pool_t *pool = r->server->process->pconf;
            cfg->sp_metadata_file = am_generate_metadata(pool, r);
#else
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "Missing MellonSPMetadataFile option.");
            apr_thread_mutex_unlock(cfg->server_mutex);
            return NULL;
#endif /* HAVE_lasso_server_new_from_buffers */
        }

        cfg->server = SERVER_NEW(cfg->sp_metadata_file,
                                 cfg->sp_private_key_file,
                                 NULL,
                                 cfg->sp_cert_file);
        if(cfg->server == NULL) {
	    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
			  "Error initializing lasso server object. Please"
			  " verify the following configuration directives:"
			  " MellonSPMetadataFile and MellonSPPrivateKeyFile.");

	    apr_thread_mutex_unlock(cfg->server_mutex);
	    return NULL;
	}

        if (am_server_add_providers(r) == 0) {
	    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
			  "Error adding IdP to lasso server object. Please"
			  " verify the following configuration directives:"
			  " MellonIdPMetadataFile and"
                          " MellonIdPPublicKeyFile.");

	    lasso_server_destroy(cfg->server);
	    cfg->server = NULL;

	    apr_thread_mutex_unlock(cfg->server_mutex);
	    return NULL;
	}
    }

    apr_thread_mutex_unlock(cfg->server_mutex);

    return cfg->server;
}


/* This function returns the first configured IdP
 *
 * Parameters:
 *  request_rec *r       The request we received.
 *
 * Returns:
 *  the providerID, or NULL if an error occured
 */
static const char *am_first_idp(request_rec *r)
{
    LassoServer *server;
    GHashTableIter iter;
    const char *idp_providerid;

    server = am_get_lasso_server(r);
    if (server == NULL)
        return NULL;

    g_hash_table_iter_init (&iter, server->providers);
    if (!g_hash_table_iter_next(&iter, (void**)&idp_providerid, NULL))
        return NULL;

    return idp_providerid;
}


/* This function selects an IdP and returns its provider_id
 *
 * Parameters:
 *  request_rec *r       The request we received.
 *
 * Returns:
 *  the provider_id, or NULL if an error occured
 */
static const char *am_get_idp(request_rec *r)
{
    LassoServer *server;
    const char *idp_provider_id;

    server = am_get_lasso_server(r);
    if (server == NULL)
        return NULL;

    /*
     * If we have a single IdP, return that one.
     */
    if (g_hash_table_size(server->providers) == 1)
        return am_first_idp(r);

    /*
     * If IdP discovery handed us an IdP, try to use it.
     */
    idp_provider_id = am_extract_query_parameter(r->pool, r->args, "IdP");
    if (idp_provider_id != NULL) {
        int rc;

        rc = am_urldecode((char *)idp_provider_id);
        if (rc != OK) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, rc, r,
                          "Could not urldecode IdP discovery value.");
            idp_provider_id = NULL;
        } else {
            if (g_hash_table_lookup(server->providers, idp_provider_id) == NULL)
                idp_provider_id = NULL;
        }

        /*
         * If we do not know about it, fall back to default.
         */
        if (idp_provider_id == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                          "IdP discovery returned unknown or inexistant IdP");
            idp_provider_id = am_first_idp(r);
        }

        return idp_provider_id;
    }

    /*
     * No IdP answered, use default
     * Perhaps we should redirect to an error page instead.
     */
    return am_first_idp(r);
}


/* This function stores dumps of the LassoIdentity and LassoSession objects
 * for the given LassoProfile object. The dumps are stored in the session
 * belonging to the current request.
 *
 * Parameters:
 *  request_rec *r         The current request.
 *  LassoProfile *profile  The profile object.
 *
 * Returns:
 *  OK on success or HTTP_INTERNAL_SERVER_ERROR on failure.
 */
static int am_save_lasso_profile_state(request_rec *r, 
                                       LassoProfile *profile, 
                                       char *saml_response)
{
    am_cache_entry_t *am_session;
    LassoIdentity *lasso_identity;
    LassoSession *lasso_session;
    gchar *identity_dump;
    gchar *session_dump;
    int ret;

    lasso_identity = lasso_profile_get_identity(profile);
    if(lasso_identity == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "The current LassoProfile object doesn't contain a"
                      " LassoIdentity object.");
        identity_dump = NULL;
    } else {
        identity_dump = lasso_identity_dump(lasso_identity);
        if(identity_dump == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "Could not create a identity dump from the"
                          " LassoIdentity object.");
            return HTTP_INTERNAL_SERVER_ERROR;
        }
    }

    lasso_session = lasso_profile_get_session(profile);
    if(lasso_session == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "The current LassoProfile object doesn't contain a"
                      " LassoSession object.");
        session_dump = NULL;
    } else {
        session_dump = lasso_session_dump(lasso_session);
        if(session_dump == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "Could not create a session dump from the"
                          " LassoSession object.");
            if(identity_dump != NULL) {
                g_free(identity_dump);
            }
            return HTTP_INTERNAL_SERVER_ERROR;
        }
    }


    am_session = am_get_request_session(r);
    if(am_session == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Could not get auth_mellon session while attempting"
                      " to store the lasso profile state.");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* Save the profile state. */
    ret = am_cache_set_lasso_state(am_session, 
                                   identity_dump, 
                                   session_dump,
                                   saml_response);

    am_release_request_session(r, am_session);


    if(identity_dump != NULL) {
        g_free(identity_dump);
    }

    if(session_dump != NULL) {
        g_free(session_dump);
    }

    return ret;
}


/* This function restores dumps of a LassoIdentity object and a LassoSession
 * object. The dumps are fetched from the session belonging to the current
 * request and restored to the given LassoProfile object.
 *
 * Parameters:
 *  request_rec *r         The current request.
 *  LassoProfile *profile  The profile object.
 *
 * Returns:
 *  OK on success or HTTP_INTERNAL_SERVER_ERROR on failure.
 */
static int am_restore_lasso_profile_state(request_rec *r,
                                          LassoProfile *profile)
{
    am_cache_entry_t *am_session;
    const char *identity_dump;
    const char *session_dump;
    int rc;


    am_session = am_get_request_session(r);
    if(am_session == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Could not get auth_mellon session while attempting"
                      " to restore the lasso profile state.");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    identity_dump = am_cache_get_lasso_identity(am_session);
    if(identity_dump != NULL) {
        rc = lasso_profile_set_identity_from_dump(profile, identity_dump);
        if(rc < 0) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "Could not restore identity from dump."
                          " Lasso error: [%i] %s", rc, lasso_strerror(rc));
            am_release_request_session(r, am_session);
            return HTTP_INTERNAL_SERVER_ERROR;
        }
    }

    session_dump = am_cache_get_lasso_session(am_session);
    if(session_dump != NULL) {
        rc = lasso_profile_set_session_from_dump(profile, session_dump);
        if(rc < 0) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "Could not restore session from dump."
                          " Lasso error: [%i] %s", rc, lasso_strerror(rc));
            am_release_request_session(r, am_session);
            return HTTP_INTERNAL_SERVER_ERROR;
        }
    }

    am_release_request_session(r, am_session);

    return OK;
}


/* This function handles an IdP initiated logout request.
 *
 * Parameters:
 *  request_rec *r       The logout request.
 *  LassoLogout *logout  A LassoLogout object initiated with
 *                       the current session.
 *
 * Returns:
 *  OK on success, or an error if any of the steps fail.
 */
static int am_handle_logout_request(request_rec *r, 
                                    LassoLogout *logout, char *msg)
{
    gint res;
    am_cache_entry_t *session;

    /* Process the logout message. Ignore missing signature. */
    res = lasso_logout_process_request_msg(logout, msg);
    if(res != 0 && res != LASSO_DS_ERROR_SIGNATURE_NOT_FOUND) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Error processing logout request message."
                      " Lasso error: [%i] %s", res, lasso_strerror(res));

        lasso_logout_destroy(logout);
        return HTTP_BAD_REQUEST;
    }

    /* Validate the logout message. Ignore missing signature. */
    res = lasso_logout_validate_request(logout);
    if(res != 0 && 
       res != LASSO_DS_ERROR_SIGNATURE_NOT_FOUND &&
       res != LASSO_PROFILE_ERROR_SESSION_NOT_FOUND) {
        ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                      "Error validating logout request."
                      " Lasso error: [%i] %s", res, lasso_strerror(res));
        /* We continue with the logout despite the error. A error could be
         * caused by the IdP believing that we are logged in when we are not.
         */
    }


    /* Search session using cookie */
    session = am_get_request_session(r);

    /* If no session found, search by NameID, for IdP initiated SOAP SLO */
    if (session == NULL) {
            LassoSaml2NameID *nameid;

            nameid = LASSO_SAML2_NAME_ID(LASSO_PROFILE(logout)->nameIdentifier);

            if (nameid != NULL)
                session = am_get_request_session_by_nameid(r, nameid->content);
    }

    /* Delete the session. */
    if (session != NULL)
        am_delete_request_session(r, session);


    /* Create response message. */
    res = lasso_logout_build_response_msg(logout);
    if(res != 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Error building logout response message."
                      " Lasso error: [%i] %s", res, lasso_strerror(res));

        lasso_logout_destroy(logout);
        return HTTP_INTERNAL_SERVER_ERROR;
    }


    /* Set redirect target. */
    apr_table_setn(r->headers_out, "Location",
		   apr_pstrdup(r->pool, LASSO_PROFILE(logout)->msg_url));

    lasso_logout_destroy(logout);

    /* HTTP_SEE_OTHER is a redirect where post-data isn't sent to the
     * new target.
     */
    return HTTP_SEE_OTHER;
}


/* This function handles a logout response message from the IdP. We get
 * this message after we have sent a logout request to the IdP.
 *
 * Parameters:
 *  request_rec *r       The logout response request.
 *  LassoLogout *logout  A LassoLogout object initiated with
 *                       the current session.
 *
 * Returns:
 *  OK on success, or an error if any of the steps fail.
 */
static int am_handle_logout_response(request_rec *r, LassoLogout *logout)
{
    gint res;
    int rc;
    am_cache_entry_t *session;
    char *return_to;

    res = lasso_logout_process_response_msg(logout, r->args);
    if(res != 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Unable to process logout response."
                      " Lasso error: [%i] %s", res, lasso_strerror(res));

        lasso_logout_destroy(logout);
        return HTTP_BAD_REQUEST;
    }

    lasso_logout_destroy(logout);

    /* Delete the session. */
    session = am_get_request_session(r);
    if(session != NULL) {
        am_delete_request_session(r, session);
    }

    return_to = am_extract_query_parameter(r->pool, r->args, "RelayState");
    if(return_to == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "No RelayState parameter to logout response handler."
                      " It is possible that your IdP doesn't support the"
                      " RelayState parameter.");
        return HTTP_BAD_REQUEST;
    }

    rc = am_urldecode(return_to);
    if(rc != OK) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rc, r,
                      "Could not urldecode RelayState value in logout"
                      " response.");
        return HTTP_BAD_REQUEST;
    }

    /* Check for bad characters in RelayState. */
    rc = am_check_url(r, return_to);
    if (rc != OK) {
        return rc;
    }

    apr_table_setn(r->headers_out, "Location", return_to);
    return HTTP_SEE_OTHER;
}


/* This function initiates a logout request and sends it to the IdP.
 *
 * Parameters:
 *  request_rec *r       The logout response request.
 *  LassoLogout *logout  A LassoLogout object initiated with
 *                       the current session.
 *
 * Returns:
 *  OK on success, or an error if any of the steps fail.
 */
static int am_init_logout_request(request_rec *r, LassoLogout *logout)
{
    char *return_to;
    int rc;
    am_cache_entry_t *mellon_session;
    gint res;
    char *redirect_to;
    LassoProfile *profile;
    LassoSession *session;
    GList *assertion_list;
    LassoNode *assertion_n;
    LassoSaml2Assertion *assertion;
    LassoSaml2AuthnStatement *authnStatement;
    LassoSamlp2LogoutRequest *request;

    return_to = am_extract_query_parameter(r->pool, r->args, "ReturnTo");
    rc = am_urldecode(return_to);
    if (rc != OK) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rc, r,
                      "Could not urldecode ReturnTo value.");
        return HTTP_BAD_REQUEST;
    }

    /* Disable the the local session (in case the IdP doesn't respond). */
    mellon_session = am_get_request_session(r);
    if(mellon_session != NULL) {
        mellon_session->logged_in = 0;
        am_release_request_session(r, mellon_session);
    }

    /* Create the logout request message. */
    res = lasso_logout_init_request(logout, NULL, LASSO_HTTP_METHOD_REDIRECT);
    if(res == LASSO_PROFILE_ERROR_SESSION_NOT_FOUND) {
        ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                      "User attempted to initiate logout without being"
                      " loggged in.");

        lasso_logout_destroy(logout);

        /* Check for bad characters in ReturnTo. */
        rc = am_check_url(r, return_to);
        if (rc != OK) {
            return rc;
        }

        /* Redirect to the page the user should be sent to after logout. */
        apr_table_setn(r->headers_out, "Location", return_to);
        return HTTP_SEE_OTHER;
    } else if(res != 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Unable to create logout request."
                      " Lasso error: [%i] %s", res, lasso_strerror(res));

        lasso_logout_destroy(logout);
        return HTTP_INTERNAL_SERVER_ERROR;
    }


    profile = LASSO_PROFILE(logout);

    /* We need to set the SessionIndex in the LogoutRequest to the SessionIndex
     * we received during the login operation. This is not needed since release
     * 2.3.0.
     */
    if (lasso_check_version(2, 3, 0, LASSO_CHECK_VERSION_NUMERIC) == 0) {
        session = lasso_profile_get_session(profile);
        assertion_list = lasso_session_get_assertions(
            session, profile->remote_providerID);
        if(! assertion_list ||
                        LASSO_IS_SAML2_ASSERTION(assertion_list->data) == FALSE) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "No assertions found for the current session.");
            lasso_logout_destroy(logout);
            return HTTP_INTERNAL_SERVER_ERROR;
        }
        /* We currently only look at the first assertion in the list
         * lasso_session_get_assertions returns.
         */
        assertion_n = assertion_list->data;

        assertion = LASSO_SAML2_ASSERTION(assertion_n);

        /* We assume that the first authnStatement contains the data we want. */
        authnStatement = LASSO_SAML2_AUTHN_STATEMENT(assertion->AuthnStatement->data);

        if(!authnStatement) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "No AuthnStatement found in the current assertion.");
            lasso_logout_destroy(logout);
            return HTTP_INTERNAL_SERVER_ERROR;
        }

        if(authnStatement->SessionIndex) {
            request = LASSO_SAMLP2_LOGOUT_REQUEST(profile->request);
            request->SessionIndex = g_strdup(authnStatement->SessionIndex);
        }
    }


    /* Set the RelayState parameter to the return url (if we have one). */
    if(return_to) {
        profile->msg_relayState = g_strdup(return_to);
    }

    /* Serialize the request message into a url which we can redirect to. */
    res = lasso_logout_build_request_msg(logout);
    if(res != 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Unable to serialize lasso logout message."
                      " Lasso error: [%i] %s", res, lasso_strerror(res));

        lasso_logout_destroy(logout);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* Set the redirect url. */
    redirect_to = apr_pstrdup(r->pool, LASSO_PROFILE(logout)->msg_url);

    /* Check if the lasso library added the RelayState. If lasso didn't add
     * a RelayState parameter, then we add one ourself. This should hopefully
     * be removed in the future.
     */
    if(return_to != NULL
       && strstr(redirect_to, "&RelayState=") == NULL
       && strstr(redirect_to, "?RelayState=") == NULL) {
        /* The url didn't contain the relaystate parameter. */
        redirect_to = apr_pstrcat(
            r->pool, redirect_to, "&RelayState=",
            am_urlencode(r->pool, return_to),
            NULL
            );
    }

    apr_table_setn(r->headers_out, "Location", redirect_to);

    lasso_logout_destroy(logout);

    /* Redirect (without including POST data if this was a POST request. */
    return HTTP_SEE_OTHER;
}


/* This function handles requests to the logout handler.
 *
 * Parameters:
 *  request_rec *r       The request.
 *
 * Returns:
 *  OK on success, or an error if any of the steps fail.
 */
static int am_handle_logout(request_rec *r)
{
    LassoServer *server;
    LassoLogout *logout;

    server = am_get_lasso_server(r);
    if(server == NULL) {
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    logout = lasso_logout_new(server);
    if(logout == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Error creating lasso logout object.");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* Restore lasso profile state. We ignore errors since we want to be able
     * to redirect the user back to the IdP even in the case of an error.
     */
    am_restore_lasso_profile_state(r, LASSO_PROFILE(logout));


    /* Check which type of request to the logout handler this is.
     * We have three types:
     * - logout requests: The IdP sends a logout request to this service.
     *                    it can be either through HTTP-Redirect or SOAP.
     * - logout responses: We have sent a logout request to the IdP, and
     *   are receiving a response.
     * - We want to initiate a logout request.
     */

    /* First check for IdP-initiated SOAP logout request */
    if ((r->args == NULL) && (r->method_number == M_POST)) {
        int rc;
        char *post_data;

        rc = am_read_post_data(r, &post_data, NULL);
        if (rc != OK) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, rc, r,
                          "Error reading POST data.");
            return HTTP_INTERNAL_SERVER_ERROR;
        }
        return am_handle_logout_request(r, logout, post_data);

    } else if(am_extract_query_parameter(r->pool, r->args, 
                                         "SAMLRequest") != NULL) {
        /* SAMLRequest - logout request from the IdP. */
        return am_handle_logout_request(r, logout, r->args);

    } else if(am_extract_query_parameter(r->pool, r->args, 
                                         "SAMLResponse") != NULL) {
        /* SAMLResponse - logout response from the IdP. */
        return am_handle_logout_response(r, logout);

    } else if(am_extract_query_parameter(r->pool, r->args, 
                                         "ReturnTo") != NULL) {
        /* RedirectTo - SP initiated logout. */
        return am_init_logout_request(r, logout);

    } else {
        /* Unknown request to the logout handler. */
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "No known parameters passed to the logout"
                      " handler. Query string was \"%s\". To initiate"
                      " a logout, you need to pass a \"ReturnTo\""
                      " parameter with a url to the web page the user should"
                      " be redirected to after a successful logout.",
                      r->args);
        return HTTP_BAD_REQUEST;
    }
}


/* This function parses a timestamp for a SAML 2.0 condition.
 *
 * Parameters:
 *  request_rec *r          The current request. Used for logging of errors.
 *  const char *timestamp   The timestamp we should parse. Must be on
 *                          the following format: "YYYY-MM-DDThh:mm:ssZ"
 *
 * Returns:
 *  An apr_time_t value with the timestamp, or 0 on error.
 */
static apr_time_t am_parse_timestamp(request_rec *r, const char *timestamp)
{
    size_t len;
    int i;
    char c;
    const char *expected;
    apr_time_exp_t time_exp;
    apr_time_t res;
    apr_status_t rc;

    len = strlen(timestamp);

    /* Verify length of timestamp. */
    if(len < 20){
        ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                      "Invalid length of timestamp: \"%s\".", timestamp);
    }

    /* Verify components of timestamp. */
    for(i = 0; i < len - 1; i++) {
        c = timestamp[i];

        expected = NULL;

        switch(i) {

        case 4:
        case 7:
            /* Matches "    -  -            " */
            if(c != '-') {
                expected = "'-'";
            }
            break;

        case 10:
            /* Matches "          T         " */
            if(c != 'T') {
                expected = "'T'";
            }
            break;

        case 13:
        case 16:
            /* Matches "             :  :   " */
            if(c != ':') {
                expected = "':'";
            }
            break;

        case 19:
            /* Matches "                   ." */
            if (c != '.') {
                expected = "'.'";
            }
            break;

        default:
            /* Matches "YYYY MM DD hh mm ss uuuuuu" */
            if(c < '0' || c > '9') {
                expected = "a digit";
            }
            break;
        }

        if(expected != NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "Invalid character in timestamp at position %i."
                          " Expected %s, got '%c'. Full timestamp: \"%s\"",
                          i, expected, c, timestamp);
            return 0;
        }
    }

    if (timestamp[len - 1] != 'Z') {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Timestamp wasn't in UTC (did not end with 'Z')."
                      " Full timestamp: \"%s\"",
                      timestamp);
        return 0;
    }


    time_exp.tm_usec = 0;
    if (len > 20) {
        /* Subsecond precision. */
        if (len > 27) {
            /* Timestamp has more than microsecond precision. Just clip it to
             * microseconds.
             */
            len = 27;
        }
        len -= 1; /* Drop the 'Z' off the end. */
        for (i = 20; i < len; i++) {
            time_exp.tm_usec = time_exp.tm_usec * 10 + timestamp[i] - '0';
        }
        for (i = len; i < 26; i++) {
            time_exp.tm_usec *= 10;
        }
    }

    time_exp.tm_sec = (timestamp[17] - '0') * 10 + (timestamp[18] - '0');
    time_exp.tm_min = (timestamp[14] - '0') * 10 + (timestamp[15] - '0');
    time_exp.tm_hour = (timestamp[11] - '0') * 10 + (timestamp[12] - '0');
    time_exp.tm_mday = (timestamp[8] - '0') * 10 + (timestamp[9] - '0');
    time_exp.tm_mon = (timestamp[5] - '0') * 10 + (timestamp[6] - '0') - 1;
    time_exp.tm_year = (timestamp[0] - '0') * 1000 +
        (timestamp[1] - '0') * 100 + (timestamp[2] - '0') * 10 +
        (timestamp[3] - '0') - 1900;

    time_exp.tm_wday = 0; /* Unknown. */
    time_exp.tm_yday = 0; /* Unknown. */

    time_exp.tm_isdst = 0; /* UTC, no daylight savings time. */
    time_exp.tm_gmtoff = 0; /* UTC, no offset from UTC. */

    rc = apr_time_exp_gmt_get(&res, &time_exp);
    if(rc != APR_SUCCESS) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rc, r,
                      "Error converting timestamp \"%s\".",
                      timestamp);
        return 0;
    }

    return res;
}


/* Validate the subject on an Assertion.
 *
 *  request_rec *r                   The current request. Used to log
 *                                   errors.
 *  LassoSaml2Assertion *assertion   The assertion we will validate.
 *  const char *url                  The current URL.
 *
 * Returns:
 *  OK on success, HTTP_BAD_REQUEST on failure.
 */
static int am_validate_subject(request_rec *r, LassoSaml2Assertion *assertion,
                               const char *url)
{
    apr_time_t now;
    apr_time_t t;
    LassoSaml2SubjectConfirmation *sc;
    LassoSaml2SubjectConfirmationData *scd;

    if (assertion->Subject == NULL) {
        /* No Subject to validate. */
        return OK;
    } else if (!LASSO_IS_SAML2_SUBJECT(assertion->Subject)) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Wrong type of Subject node.");
        return HTTP_BAD_REQUEST;
    }

    if (assertion->Subject->SubjectConfirmation == NULL) {
        /* No SubjectConfirmation. */
        return OK;
    } else if (!LASSO_IS_SAML2_SUBJECT_CONFIRMATION(assertion->Subject->SubjectConfirmation)) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Wrong type of SubjectConfirmation node.");
        return HTTP_BAD_REQUEST;
    }

    sc = assertion->Subject->SubjectConfirmation;
    if (sc->Method == NULL ||
        strcmp(sc->Method, "urn:oasis:names:tc:SAML:2.0:cm:bearer")) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Invalid Method in SubjectConfirmation.");
        return HTTP_BAD_REQUEST;
    }

    scd = sc->SubjectConfirmationData;
    if (scd == NULL) {
        /* Nothing to verify. */
        return OK;
    } else if (!LASSO_IS_SAML2_SUBJECT_CONFIRMATION_DATA(scd)) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Wrong type of SubjectConfirmationData node.");
        return HTTP_BAD_REQUEST;
    }

    now = apr_time_now();

    if (scd->NotBefore) {
        t = am_parse_timestamp(r, scd->NotBefore);
        if (t == 0) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "Invalid timestamp in NotBefore in SubjectConfirmationData.");
            return HTTP_BAD_REQUEST;
        }
        if (t - 60000000 > now) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "NotBefore in SubjectConfirmationData was in the future.");
            return HTTP_BAD_REQUEST;
        }
    }

    if (scd->NotOnOrAfter) {
        t = am_parse_timestamp(r, scd->NotOnOrAfter);
        if (t == 0) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "Invalid timestamp in NotOnOrAfter in SubjectConfirmationData.");
            return HTTP_BAD_REQUEST;
        }
        if (now >= t + 60000000) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "NotOnOrAfter in SubjectConfirmationData was in the past.");
            return HTTP_BAD_REQUEST;
        }
    }

    if (scd->Recipient) {
        if (strcmp(scd->Recipient, url)) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "Wrong Recipient in SubjectConfirmationData. Current URL is: %s, Recipient is %s",
                          url, scd->Recipient);
            return HTTP_BAD_REQUEST;
        }
    }

    if (scd->Address) {
        if (strcasecmp(scd->Address, r->connection->remote_ip)) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "Wrong Address in SubjectConfirmationData."
                          "Current address is \"%s\", but should have been \"%s\".",
                          r->connection->remote_ip, scd->Address);
            return HTTP_BAD_REQUEST;
        }
    }

    return OK;
}


/* Validate the conditions on an Assertion.
 *
 * Parameters:
 *  request_rec *r                   The current request. Used to log
 *                                   errors.
 *  LassoSaml2Assertion *assertion   The assertion we will validate.
 *  const char *providerID           The providerID of the SP.
 *
 * Returns:
 *  OK on success, HTTP_BAD_REQUEST on failure.
 */
static int am_validate_conditions(request_rec *r,
                                  LassoSaml2Assertion *assertion,
                                  const char *providerID)
{
    LassoSaml2Conditions *conditions;
    apr_time_t now;
    apr_time_t t;
    GList *i;
    LassoSaml2AudienceRestriction *ar;

    conditions = assertion->Conditions;
    if (!LASSO_IS_SAML2_CONDITIONS(conditions)) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Wrong type of Conditions node.");
        return HTTP_BAD_REQUEST;
    }

    if (conditions->Condition != NULL) {
        /* This is a list of LassoSaml2ConditionAbstract - if it
         * isn't empty, we have an unsupported condition.
         */
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Unsupported condition in Assertion.");
        return HTTP_BAD_REQUEST;
    }


    now = apr_time_now();

    if (conditions->NotBefore) {
        t = am_parse_timestamp(r, conditions->NotBefore);
        if (t == 0) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "Invalid timestamp in NotBefore in Condition.");
            return HTTP_BAD_REQUEST;
        }
        if (t - 60000000 > now) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "NotBefore in Condition was in the future.");
            return HTTP_BAD_REQUEST;
        }
    }

    if (conditions->NotOnOrAfter) {
        t = am_parse_timestamp(r, conditions->NotOnOrAfter);
        if (t == 0) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "Invalid timestamp in NotOnOrAfter in Condition.");
            return HTTP_BAD_REQUEST;
        }
        if (now >= t + 60000000) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "NotOnOrAfter in Condition was in the past.");
            return HTTP_BAD_REQUEST;
        }
    }

    for (i = g_list_first(conditions->AudienceRestriction); i != NULL;
         i = g_list_next(i)) {
        ar = i->data;
        if (!LASSO_IS_SAML2_AUDIENCE_RESTRICTION(ar)) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "Wrong type of AudienceRestriction node.");
            return HTTP_BAD_REQUEST;
        }

        if (ar->Audience == NULL || strcmp(ar->Audience, providerID)) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "Invalid Audience in Conditions. Should be: %s",
                          providerID);
            return HTTP_BAD_REQUEST;
        }
    }

    return OK;
}



/* This function sets the session expire timestamp based on NotOnOrAfter
 * attribute of a condition element.
 *
 * Parameters:
 *  request_rec *r                   The current request. Used to log
 *                                   errors.
 *  am_cache_entry_t *session        The current session.
 *  LassoSaml2Assertion *assertion   The assertion which we will extract
 *                                   the conditions from.
 *
 * Returns:
 *  Nothing.
 */
static void am_handle_session_expire(request_rec *r, am_cache_entry_t *session,
                                LassoSaml2Assertion *assertion)
{
    GList *authn_itr;
    LassoSaml2AuthnStatement *authn;
    const char *not_on_or_after;
    apr_time_t t;

    for(authn_itr = g_list_first(assertion->AuthnStatement); authn_itr != NULL;
        authn_itr = g_list_next(authn_itr)) {

        authn = authn_itr->data;
        if (!LASSO_IS_SAML2_AUTHN_STATEMENT(authn)) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "Wrong type of AuthnStatement node.");
            continue;
        }

        /* Find timestamp. */
        not_on_or_after = authn->SessionNotOnOrAfter;
        if(not_on_or_after == NULL) {
            continue;
        }


        /* Parse timestamp. */
        t = am_parse_timestamp(r, not_on_or_after);
        if(t == 0) {
            continue;
        }

        /* Updates the expires timestamp if this one is earlier than the
         * previous timestamp.
         */
        am_cache_update_expires(session, t);
    }
}


/* This function is for decoding and storing attributes with the feide
 * encoding. It takes in an attribute name and a value. The value is split
 * into multiple values. We base64 decode these values, and store them in the
 * session data.
 *
 * Parameters:
 *  request_rec *r              The current request.
 *  am_cache_entry_t *session   The current session.
 *  const char *name            Name of the attribute.
 *  const char *value           The value(s) of the attribute.
 *
 * Returns:
 *  OK on success or an error from am_cache_env_append(...) if it was unable
 *  to store the attribute.
 */
static int am_store_attribute_feide(request_rec *r, am_cache_entry_t *session,
                                    const char *name, const char *value)
{
    char *edit_value;
    char *start; 
    char *next;
    int len;
    int ret;

    /* We need to be able to change the value. */
    edit_value = apr_pstrdup(r->pool, value);

    for(start = edit_value; start != NULL; start = next) {
        /* The values are separated by '_'. */
        next = strchr(start, '_');

        if(next != NULL) {
            /* Insert null-terminator after current value. */
            *next = '\0';

            /* The next value begins at next+1. */
            next++;
        }

        /* Now start points to the current value, which we have
         * null-terminated. next points to the next value, or NULL if
         * this is the last value.
         */

        /* base64-decode current value.
         * From looking at the source of apr_base64_decode_binary, it
         * appears to be safe to use in-place.
         */
        len = apr_base64_decode_binary((unsigned char *)start, start);

        /* Add null-terminator at end of string. */
        start[len] = '\0';


        /* Store current name-value-pair. */
        ret = am_cache_env_append(session, name, start);
        if(ret != OK) {
            return ret;
        }
    }

    return OK;
}


/* This function is for storing attributes without any encoding. We just store
 * the attribute as it is.
 *
 * Parameters:
 *  request_rec *r              The current request.
 *  am_cache_entry_t *session   The current session.
 *  const char *name            The name of the attribute.
 *  const char *value           The value of the attribute.
 *
 * Returns:
 *  OK on success or an error from am_cache_env_append(...) if it failed.
 */
static int am_store_attribute_none(request_rec *r, am_cache_entry_t *session,
                                   const char *name, const char *value)
{
    /* Store current name-value-pair. */
    return am_cache_env_append(session, name, value);
}


/* This function passes a name-value pair to the decoder selected by the
 * MellonDecoder configuration option. The decoder will decode the value
 * and store it in the session data.
 *
 * Parameters:
 *  request_rec *r              The current request.
 *  am_cache_entry_t *session   The current session.
 *  const char *name            The name of the attribute.
 *  const char *value           The value of the attribute.
 *
 * Returns:
 *  OK on success or an error from the attribute decoder if it failed.
 */
static int am_store_attribute(request_rec *r, am_cache_entry_t *session,
                              const char *name, const char *value)
{
    am_dir_cfg_rec *dir_cfg;

    dir_cfg = am_get_dir_cfg(r);

    switch(dir_cfg->decoder) {
    case am_decoder_none:
        return am_store_attribute_none(r, session, name, value);

    case am_decoder_feide:
        return am_store_attribute_feide(r, session, name, value);

    default:
        return am_store_attribute_none(r, session, name, value);
    }
}


/* Add all the attributes from an assertion to the session data for the
 * current user.
 *
 * Parameters:
 *  am_cache_entry_t *s             The current session.
 *  request_rec *r                  The current request.
 *  const char *name_id             The name identifier we received from
 *                                  the IdP.
 *  LassoSaml2Assertion *assertion  The assertion.
 *
 * Returns:
 *  HTTP_BAD_REQUEST if we couldn't find the session id of the user, or
 *  OK if no error occured.
 */
static int add_attributes(am_cache_entry_t *session, request_rec *r,
                          const char *name_id, LassoSaml2Assertion *assertion)
{
    am_dir_cfg_rec *dir_cfg;
    GList *atr_stmt_itr;
    LassoSaml2AttributeStatement *atr_stmt;
    GList *atr_itr;
    LassoSaml2Attribute *attribute;
    GList *value_itr;
    LassoSaml2AttributeValue *value;
    LassoMiscTextNode *value_text;
    int ret;

    dir_cfg = am_get_dir_cfg(r);

    /* Set expires to whatever is set by MellonSessionLength. */
    if(dir_cfg->session_length == -1) {
        /* -1 means "use default. The current default is 86400 seconds. */
        am_cache_update_expires(session, apr_time_now()
                                + apr_time_make(86400, 0));
    } else {
        am_cache_update_expires(session, apr_time_now()
                                + apr_time_make(dir_cfg->session_length, 0));
    }

    /* Mark user as logged in. */
    session->logged_in = 1;

    /* Save session information. */
    ret = am_cache_env_append(session, "NAME_ID", name_id);
    if(ret != OK) {
        return ret;
    }

    /* Update expires timestamp of session. */
    am_handle_session_expire(r, session, assertion);

    /* assertion->AttributeStatement is a list of
     * LassoSaml2AttributeStatement objects.
     */
    for(atr_stmt_itr = g_list_first(assertion->AttributeStatement);
        atr_stmt_itr != NULL;
        atr_stmt_itr = g_list_next(atr_stmt_itr)) {

        atr_stmt = atr_stmt_itr->data;
        if (!LASSO_IS_SAML2_ATTRIBUTE_STATEMENT(atr_stmt)) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "Wrong type of AttributeStatement node.");
            continue;
        }

        /* atr_stmt->Attribute is list of LassoSaml2Attribute objects. */
        for(atr_itr = g_list_first(atr_stmt->Attribute);
            atr_itr != NULL;
            atr_itr = g_list_next(atr_itr)) {

            attribute = atr_itr->data;
            if (!LASSO_IS_SAML2_ATTRIBUTE(attribute)) {
                ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                              "Wrong type of Attribute node.");
                continue;
            }

            /* attribute->AttributeValue is a list of
             * LassoSaml2AttributeValue objects.
             */
            for(value_itr = g_list_first(attribute->AttributeValue);
                value_itr != NULL;
                value_itr = g_list_next(value_itr)) {

                value = value_itr->data;
                if (!LASSO_IS_SAML2_ATTRIBUTE_VALUE(value)) {
                    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                                  "Wrong type of AttributeValue node.");
                    continue;
                }

                /* value->any is a list with the child nodes of the
                 * AttributeValue element.
                 *
                 * We assume that the list contains a single text node.
                 */
                if(value->any == NULL) {
                    ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                                  "AttributeValue element was empty.");
                    continue;
                }

                /* Verify that this is a LassoMiscTextNode object. */
                if(!LASSO_IS_MISC_TEXT_NODE(value->any->data)) {
                    ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                                  "AttributeValue element contained an "
                                  " element which wasn't a text node.");
                    continue;
                }

                value_text = LASSO_MISC_TEXT_NODE(value->any->data);


                /* Decode and save the attribute. */
                ret = am_store_attribute(r, session, attribute->Name,
                                         value_text->content);
                if(ret != OK) {
                    return ret;
                }
            }
        }
    }

    return OK;
}


/* This function finishes handling of a login response after it has been parsed
 * by the HTTP-POST or HTTP-Artifact handler.
 *
 * Parameters:
 *  request_rec *r       The current request.
 *  LassoLogin *login    The login object which has been initialized with the
 *                       data we have received from the IdP.
 *  char *relay_state    The RelayState parameter from the POST data or from
 *                       the request url. This parameter is urlencoded, and
 *                       this function will urldecode it in-place. Therefore it
 *                       must be possible to overwrite the data.
 *
 * Returns:
 *  A HTTP status code which should be returned to the client.
 */
static int am_handle_reply_common(request_rec *r, LassoLogin *login,
                                  char *relay_state, char *saml_response)
{
    char *url;
    char *chr;
    const char *name_id;
    LassoSamlp2Response *response;
    LassoSaml2Assertion *assertion;
    const char *in_response_to;
    am_dir_cfg_rec *dir_cfg;
    am_cache_entry_t *session;
    int rc;
    const char *idp;

    url = am_reconstruct_url(r);
    chr = strchr(url, '?');
    if (! chr) {
        chr = strchr(url, ';');
    }
    if (chr) {
        *chr = '\0';
    }


    dir_cfg = am_get_dir_cfg(r);

    if(LASSO_PROFILE(login)->nameIdentifier == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "No acceptable name identifier found in"
                      " SAML 2.0 response.");
        lasso_login_destroy(login);
        return HTTP_BAD_REQUEST;
    }

    name_id = LASSO_SAML2_NAME_ID(LASSO_PROFILE(login)->nameIdentifier)
        ->content;

    response = LASSO_SAMLP2_RESPONSE(LASSO_PROFILE(login)->response);

    if (response->parent.Destination) {
        if (strcmp(response->parent.Destination, url)) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "Invalid Destination on Response. Should be: %s",
                          url);
            lasso_login_destroy(login);
            return HTTP_BAD_REQUEST;
        }
    }

    if (g_list_length(response->Assertion) == 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "No Assertion in response.");
        lasso_login_destroy(login);
        return HTTP_BAD_REQUEST;
    }
    if (g_list_length(response->Assertion) > 1) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "More than one Assertion in response.");
        lasso_login_destroy(login);
        return HTTP_BAD_REQUEST;
    }
    assertion = g_list_first(response->Assertion)->data;
    if (!LASSO_IS_SAML2_ASSERTION(assertion)) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Wrong type of Assertion node.");
        lasso_login_destroy(login);
        return HTTP_BAD_REQUEST;
    }

    rc = am_validate_subject(r, assertion, url);
    if (rc != OK) {
        lasso_login_destroy(login);
        return rc;
    }

    rc = am_validate_conditions(r, assertion,
        LASSO_PROVIDER(LASSO_PROFILE(login)->server)->ProviderID);

    if (rc != OK) {
        lasso_login_destroy(login);
        return rc;
    }

    in_response_to = response->parent.InResponseTo;


    if(in_response_to != NULL) {
        /* This is SP-initiated login. Check that we have a cookie. */
        if(am_cookie_get(r) == NULL) {
            /* Missing cookie. */
            ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r,
                          "User has disabled cookies, or has lost"
                          " the cookie before returning from the SAML2"
                          " login server.");
            if(dir_cfg->no_cookie_error_page != NULL) {
                apr_table_setn(r->headers_out, "Location",
                               dir_cfg->no_cookie_error_page);
                lasso_login_destroy(login);
                return HTTP_SEE_OTHER;
            } else {
                /* Return 400 Bad Request when the user hasn't set a
                 * no-cookie error page.
                 */
                lasso_login_destroy(login);
                return HTTP_BAD_REQUEST;
            }
        }
    }

    /* Create a new session. */
    session = am_new_request_session(r);
    if(session == NULL) {
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,
                    "am_new_request_session() failed");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    rc = add_attributes(session, r, name_id, assertion);
    if(rc != OK) {
        am_release_request_session(r, session);
        lasso_login_destroy(login);
        return rc;
    }

    /* If requested, save the IdP ProviderId */
    if(dir_cfg->idpattr != NULL) {
        idp = LASSO_PROFILE(login)->remote_providerID;
        if(idp != NULL) {
            rc = am_cache_env_append(session, dir_cfg->idpattr, idp);
            if(rc != OK) {
                lasso_login_destroy(login);
                return rc;
            }
        }
    }

    am_release_request_session(r, session);

    rc = lasso_login_accept_sso(login);
    if(rc < 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Unable to accept SSO message."
                      " Lasso error: [%i] %s", rc, lasso_strerror(rc));
        lasso_login_destroy(login);
        return HTTP_INTERNAL_SERVER_ERROR;
    }


    /* Save the profile state. */
    rc = am_save_lasso_profile_state(r, LASSO_PROFILE(login), saml_response);
    if(rc != OK) {
        lasso_login_destroy(login);
        return rc;
    }

    lasso_login_destroy(login);


    /* No RelayState - we don't know what to do. Use default login path. */
    if(relay_state == NULL) {
       dir_cfg = am_get_dir_cfg(r);
       apr_table_setn(r->headers_out, "Location", dir_cfg->login_path);
       return HTTP_SEE_OTHER;
    }

    rc = am_urldecode(relay_state);
    if (rc != OK) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rc, r,
                      "Could not urldecode RelayState value.");
        return HTTP_BAD_REQUEST;
    }

    /* Check for bad characters in RelayState. */
    rc = am_check_url(r, relay_state);
    if (rc != OK) {
        return rc;
    }

    apr_table_setn(r->headers_out, "Location",
                   relay_state);

    /* HTTP_SEE_OTHER should be a redirect where the browser doesn't repeat
     * the POST data to the new page.
     */
    return HTTP_SEE_OTHER;
}


/* This function handles responses to login requests received with the
 * HTTP-POST binding.
 *
 * Parameters:
 *  request_rec *r       The request we received.
 *
 * Returns:
 *  HTTP_SEE_OTHER on success, or an error on failure.
 */
static int am_handle_post_reply(request_rec *r)
{
    int rc;
    char *post_data;
    char *saml_response;
    LassoServer *server;
    LassoLogin *login;
    char *relay_state;

    /* Make sure that this is a POST request. */
    if(r->method_number != M_POST) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Exptected POST request for HTTP-POST endpoint."
                      " Got a %s request instead.", r->method);

        /* According to the documentation for request_rec, a handler which
         * doesn't handle a request method, should set r->allowed to the
         * methods it handles, and return DECLINED.
         * However, the default handler handles GET-requests, so for GET
         * requests the handler should return HTTP_METHOD_NOT_ALLOWED.
         */
        r->allowed = M_POST;

        if(r->method_number == M_GET) {
            return HTTP_METHOD_NOT_ALLOWED;
        } else {
            return DECLINED;
        }
    }

    /* Read POST-data. */
    rc = am_read_post_data(r, &post_data, NULL);
    if (rc != OK) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rc, r,
                      "Error reading POST data.");
        return rc;
    }

    /* Extract the SAMLResponse-field from the data. */
    saml_response = am_extract_query_parameter(r->pool, post_data,
                                            "SAMLResponse");
    if (saml_response == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rc, r,
                      "Could not find SAMLResponse field in POST data.");
        return HTTP_BAD_REQUEST;
    }

    rc = am_urldecode(saml_response);
    if (rc != OK) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rc, r,
                      "Could not urldecode SAMLResponse value.");
        return rc;
    }

    server = am_get_lasso_server(r);
    if(server == NULL) {
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    login = lasso_login_new(server);
    if (login == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Failed to initialize LassoLogin object.");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* Process login responce. */
    rc = lasso_login_process_authn_response_msg(login, saml_response);
    if (rc != 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Error processing authn response."
                      " Lasso error: [%i] %s", rc, lasso_strerror(rc));

        lasso_login_destroy(login);
        return HTTP_BAD_REQUEST;
    }

    /* Extract RelayState parameter. */
    relay_state = am_extract_query_parameter(r->pool, post_data,
                                               "RelayState");

    /* Finish handling the reply with the common handler. */
    return am_handle_reply_common(r, login, relay_state, saml_response);
}


/* This function handles responses to login requests which use the
 * HTTP-Artifact binding.
 *
 * Parameters:
 *  request_rec *r       The request we received.
 *
 * Returns:
 *  HTTP_SEE_OTHER on success, or an error on failure.
 */
static int am_handle_artifact_reply(request_rec *r)
{
    int rc;
    LassoServer *server;
    LassoLogin *login;
    char *response;
    char *relay_state;

    /* Make sure that this is a GET request. */
    if(r->method_number != M_GET) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Exptected GET request for the HTTP-Artifact endpoint."
                      " Got a %s request instead.", r->method);

        /* According to the documentation for request_rec, a handler which
         * doesn't handle a request method, should set r->allowed to the
         * methods it handles, and return DECLINED.
         * However, the default handler handles GET-requests, so for GET
         * requests the handler should return HTTP_METHOD_NOT_ALLOWED.
         * This endpoints handles GET requests, so it isn't necessary to
         * check for method_number == M_GET.
         */
        r->allowed = M_GET;

        return DECLINED;
    }

    server = am_get_lasso_server(r);
    if(server == NULL) {
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    login = lasso_login_new(server);
    if (login == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Failed to initialize LassoLogin object.");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* Parse artifact url. */
    rc = lasso_login_init_request(login, r->args,
                                  LASSO_HTTP_METHOD_ARTIFACT_GET);
    if(rc < 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Failed to handle login response."
                      " Lasso error: [%i] %s", rc, lasso_strerror(rc));
        lasso_login_destroy(login);
        return HTTP_BAD_REQUEST;
    }

    /* Prepare SOAP request. */
    rc = lasso_login_build_request_msg(login);
    if(rc < 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Failed to prepare SOAP message for HTTP-Artifact"
                      " resolution."
                      " Lasso error: [%i] %s", rc, lasso_strerror(rc));
        lasso_login_destroy(login);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* Do the SOAP request. */
    rc = am_httpclient_post_str(
        r,
        LASSO_PROFILE(login)->msg_url,
        LASSO_PROFILE(login)->msg_body,
        "text/xml",
        (void**)&response,
        NULL
        );
    if(rc != OK) {
        lasso_login_destroy(login);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    rc = lasso_login_process_response_msg(login, response);
    if(rc != 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Failed to handle HTTP-Artifact response data."
                      " Lasso error: [%i] %s", rc, lasso_strerror(rc));
        lasso_login_destroy(login);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* Extract the RelayState parameter. */
    relay_state = am_extract_query_parameter(r->pool, r->args,
                                               "RelayState");

    /* Finish handling the reply with the common handler. */
    return am_handle_reply_common(r, login, relay_state, "");
}



/* This function builds web form inputs for a saved POST request, 
 * in multipart/form-data format.
 *
 * Parameters:
 *  request_rec *r        The request
 *  const char *post_data The savec POST request
 *
 * Returns:
 *  The web form fragment, or NULL on failure.
 */
const char *am_post_mkform_multipart(request_rec *r, const char *post_data)
{
    const char *mime_part;
    const char *boundary;
    char *l1;
    char *post_form = "";

    /* Replace CRLF by LF */
    post_data = am_strip_cr(r, post_data);

    if ((boundary = am_xstrtok(r, post_data, "\n", &l1)) == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                     "Cannot figure initial boundary");
        return NULL;
    }

    for (mime_part = am_xstrtok(r, post_data, boundary, &l1); mime_part;
         mime_part = am_xstrtok(r, NULL, boundary, &l1)) {
        const char *hdr;
        const char *name = NULL;
        const char *value = NULL;
        const char *input_item;

        /* End of MIME data */
        if (strcmp(mime_part, "--\n") == 0)
            break;

        /* Remove leading CRLF */
        if (strstr(mime_part, "\n") == mime_part)
            mime_part += 1;

        /* Empty part */
        if (*mime_part == '\0')
            continue;

        /* Find Content-Disposition header 
         * Looking for 
         * Content-Disposition: form-data; name="the_name"\n 
         */
        hdr = am_get_mime_header(r, mime_part, "Content-Disposition");
        if (hdr == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                         "No Content-Disposition header in MIME section,");
            continue;
        }

        name = am_get_header_attr(r, hdr, "form-data", "name");
        if (name == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                         "Unexpected Content-Disposition header: \"%s\"", hdr);
            continue;
        }

        if ((value = am_get_mime_body(r, mime_part)) == NULL)
            value = "";

        input_item = apr_psprintf(r->pool, 
                    "    <input type=\"hidden\" name=\"%s\" value=\"%s\">\n",
                    am_htmlencode(r, name), am_htmlencode(r, value));
        post_form = apr_pstrcat(r->pool, post_form, input_item, NULL);
    }

    return post_form;
}

/* This function builds web form inputs for a saved POST request, 
 * in application/x-www-form-urlencoded format
 *
 * Parameters:
 *  request_rec *r        The request
 *  const char *post_data The savec POST request
 *
 * Returns:
 *  The web form fragment, or NULL on failure.
 */
const char *am_post_mkform_urlencoded(request_rec *r, const char *post_data)
{
    const char *item;
    char *last;
    char *post_form = "";

    for (item = am_xstrtok(r, post_data, "&", &last); item; 
         item = am_xstrtok(r, NULL, "&", &last)) {
        char *l1;
        char *name;
        char *value;
        const char *input_item;

        name = (char *)am_xstrtok(r, item, "=", &l1);  
        value = (char *)am_xstrtok(r, NULL, "=", &l1);

        if (name == NULL)
            continue;

        if (value == NULL)
            value = (char *)"";

        if (am_urldecode(name) != OK) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                         "urldecode(\"%s\") failed", name);
            return NULL;
        }

        if (am_urldecode(value) != OK) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                         "urldecode(\"%s\") failed", value);
            return NULL;
        }

        input_item = apr_psprintf(r->pool, 
                    "    <input type=\"hidden\" name=\"%s\" value=\"%s\">\n",
                    am_htmlencode(r, name), am_htmlencode(r, value));
        post_form = apr_pstrcat(r->pool, post_form, input_item, NULL);
    }
    return post_form;
}


/* This function handles responses to repost request
 *
 * Parameters:
 *  request_rec *r       The request we received.
 *
 * Returns:
 *  OK on success, or an error on failure.
 */
static int am_handle_repost(request_rec *r)
{
    am_mod_cfg_rec *mod_cfg;
    const char *query;
    const char *enctype;
    char *charset;
    char *psf_id;
    char *cp;
    char *psf_filename;
    char *post_data;
    const char *post_form;
    char *output;
    char *return_url;
    const char *(*post_mkform)(request_rec *, const char *);

    if (am_cookie_get(r) == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                      "Repost query without a session");
        return HTTP_FORBIDDEN;
    }

    mod_cfg = am_get_mod_cfg(r->server);
    query = r->parsed_uri.query;
    
    enctype = am_extract_query_parameter(r->pool, query, "enctype");
    if (enctype == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                      "Bad repost query: missing enctype");
        return HTTP_BAD_REQUEST;
    }
    if (strcmp(enctype, "urlencoded") == 0) {
        enctype = "application/x-www-form-urlencoded";
        post_mkform = am_post_mkform_urlencoded;
    } else if (strcmp(enctype, "multipart") == 0) {
        enctype = "multipart/form-data";
        post_mkform = am_post_mkform_multipart;
    } else {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                      "Bad repost query: invalid enctype \"%s\".", enctype);
        return HTTP_BAD_REQUEST;
    }

    charset = am_extract_query_parameter(r->pool, query, "charset");
    if (charset != NULL) {
        if (am_urldecode(charset) != OK) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                          "Bad repost query: invalid charset \"%s\"", charset);
            return HTTP_BAD_REQUEST;
        }
    
        /* Check that charset is sane */
        for (cp = charset; *cp; cp++) {
            if (!apr_isalnum(*cp) && (*cp != '-') && (*cp != '_')) {
                ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                              "Bad repost query: invalid charset \"%s\"", charset);
                return HTTP_BAD_REQUEST;
            }
        }
    }

    psf_id = am_extract_query_parameter(r->pool, query, "id");
    if (psf_id == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                      "Bad repost query: missing id");
        return HTTP_BAD_REQUEST;
    }

    /* Check that Id is sane */
    for (cp = psf_id; *cp; cp++) {
        if (!apr_isalnum(*cp)) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                          "Bad repost query: invalid id \"%s\"", psf_id);
            return HTTP_BAD_REQUEST;
        }
    }
    
    
    return_url = am_extract_query_parameter(r->pool, query, "ReturnTo");
    if (return_url == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Invalid or missing query ReturnTo parameter.");
        return HTTP_BAD_REQUEST;
    }

    if (am_urldecode(return_url) != OK) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "Bad repost query: return");
        return HTTP_BAD_REQUEST;
    }

    psf_filename = apr_psprintf(r->pool, "%s/%s", mod_cfg->post_dir, psf_id); 
    if ((post_data = am_getfile(r->pool, r->server, psf_filename)) == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                      "Bad repost query: cannot find \"%s\"", psf_filename);
        return HTTP_BAD_REQUEST;
    }

    if ((post_form = (*post_mkform)(r, post_data)) == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "am_post_mkform() failed");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    if (charset != NULL) {
         r->content_type = apr_psprintf(r->pool, 
                                        "text/html; charset=\"%s\"",
                                        charset);
         charset = apr_psprintf(r->pool, " accept-charset=\"%s\"", charset);
    } else {
         r->content_type = "text/html";
         charset = (char *)"";
    }
    
    output = apr_psprintf(r->pool,
      "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\">\n"
      "<html>\n"
      " <head>\n" 
      "  <title>SAML rePOST request</title>\n" 
      " </head>\n" 
      " <body onload=\"document.getElementById('form').submit();\">\n" 
      "  <noscript>\n"
      "   Your browser does not support Javascript, \n"
      "   you must click the button below to proceed.\n"
      "  </noscript>\n"
      "   <form id=\"form\" method=\"POST\" action=\"%s\" enctype=\"%s\"%s>\n%s"
      "    <noscript>\n"
      "     <input type=\"submit\">\n"
      "    </noscript>\n"
      "   </form>\n"
      " </body>\n" 
      "</html>\n",
      am_htmlencode(r, return_url), enctype, charset, post_form);

    ap_rputs(output, r);
    return OK;
}


/* This function handles responses to metadata request
 *
 * Parameters:
 *  request_rec *r       The request we received.
 *
 * Returns:
 *  OK on success, or an error on failure.
 */
static int am_handle_metadata(request_rec *r)
{
#ifdef HAVE_lasso_server_new_from_buffers
    am_dir_cfg_rec *cfg = am_get_dir_cfg(r);
    LassoServer *server;
    const char *data;

    server = am_get_lasso_server(r);
    if(server == NULL)
        return HTTP_INTERNAL_SERVER_ERROR;

    data = cfg->sp_metadata_file;
    if (data == NULL)
        return HTTP_INTERNAL_SERVER_ERROR;

    r->content_type = "application/samlmetadata+xml";

    ap_rputs(data, r);

    return OK;
#else  /* ! HAVE_lasso_server_new_from_buffers */

    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                  "metadata publishing require lasso 2.2.2 or higher");
    return HTTP_NOT_FOUND;
#endif
}

/* This function handles responses to request on our endpoint
 *
 * Parameters:
 *  request_rec *r       The request we received.
 *
 * Returns:
 *  OK on success, or an error on failure.
 */
int am_handler(request_rec *r)
{
    am_dir_cfg_rec *cfg = am_get_dir_cfg(r);
    const char *endpoint;

    /* Check if this is a request for one of our endpoints. We check if
     * the uri starts with the path set with the MellonEndpointPath
     * configuration directive.
     */
    if(strstr(r->uri, cfg->endpoint_path) != r->uri)
        return DECLINED;

    /* Make sure that this is a GET request. */
    if(r->method_number != M_GET) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Exptected GET request for the metadata endpoint."
                      " Got a %s request instead.", r->method);

        /* According to the documentation for request_rec, a handler which
         * doesn't handle a request method, should set r->allowed to the
         * methods it handles, and return DECLINED.
         * However, the default handler handles GET-requests, so for GET
         * requests the handler should return HTTP_METHOD_NOT_ALLOWED.
         * This endpoints handles GET requests, so it isn't necessary to
         * check for method_number == M_GET.
         */
        r->allowed = M_GET;

        return DECLINED;
    }

    endpoint = &r->uri[strlen(cfg->endpoint_path)];
    if (strcmp(endpoint, "metadata") == 0)
        return am_handle_metadata(r);
    else if (strcmp(endpoint, "repost") == 0)
        return am_handle_repost(r);
    else
        return DECLINED;
}


/* Create and send an authentication request.
 *
 * Parameters:
 *  request_rec *r         The request we are processing.
 *  const char *idp        The entityID of the IdP.
 *  const char *return_to  The URL we should redirect to when receiving the request.
 *  int is_passive         Whether to send a passive request.
 *
 * Returns:
 *  HTTP_SEE_OTHER on success, or an error on failure.
 */
static int am_send_authn_request(request_rec *r, const char *idp,
                           const char *return_to, int is_passive)
{
    LassoServer *server;
    LassoLogin *login;
    LassoSamlp2AuthnRequest *request;
    gint ret;
    char *redirect_to;

    /* Add cookie for cookie test. We know that we should have
     * a valid cookie when we return from the IdP after SP-initiated
     * login.
     */
    am_cookie_set(r, "cookietest");


    server = am_get_lasso_server(r);
    if(server == NULL) {
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    login = lasso_login_new(server);
    if(login == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
		      "Error creating LassoLogin object from LassoServer.");
	return HTTP_INTERNAL_SERVER_ERROR;
    }

    ret = lasso_login_init_authn_request(login, idp,
					 LASSO_HTTP_METHOD_REDIRECT);
    if(ret != 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Error creating login request."
                      " Lasso error: [%i] %s", ret, lasso_strerror(ret));
	lasso_login_destroy(login);
	return HTTP_INTERNAL_SERVER_ERROR;
    }

    request = LASSO_SAMLP2_AUTHN_REQUEST(LASSO_PROFILE(login)->request);
    if(request->NameIDPolicy == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Error creating login request. Please verify the "
                      "MellonSPMetadataFile directive.");
        lasso_login_destroy(login);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    request->ForceAuthn = FALSE;
    request->IsPassive = is_passive;

    request->NameIDPolicy->AllowCreate = TRUE;

    LASSO_SAMLP2_REQUEST_ABSTRACT(request)->Consent
      = g_strdup(LASSO_SAML2_CONSENT_IMPLICIT);


    /*
     * Make sure the Destination attribute is set to the IdP
     * SingleSignOnService endpoint. This is required for
     * Shibboleth 2 interoperability, and older versions of
     * lasso (at least up to 2.2.91) did not do it.
     * XXX Here we assume HTTP-Redirect method
     */
    if (LASSO_SAMLP2_REQUEST_ABSTRACT(request)->Destination == NULL)
        LASSO_SAMLP2_REQUEST_ABSTRACT(request)->Destination =
            g_strdup(am_get_service_url(r, LASSO_PROFILE(login),
                                        "SingleSignOnService HTTP-Redirect"));

    LASSO_PROFILE(login)->msg_relayState = g_strdup(return_to);

    ret = lasso_login_build_authn_request_msg(login);
    if(ret != 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Error building login request."
                      " Lasso error: [%i] %s", ret, lasso_strerror(ret));
	lasso_login_destroy(login);
	return HTTP_INTERNAL_SERVER_ERROR;
    }


    redirect_to = apr_pstrdup(r->pool, LASSO_PROFILE(login)->msg_url);

    /* Check if the lasso library added the RelayState. If lasso didn't add
     * a RelayState parameter, then we add one ourself. This should hopefully
     * be removed in the future.
     */
    if(strstr(redirect_to, "&RelayState=") == NULL
       && strstr(redirect_to, "?RelayState=") == NULL) {
        /* The url didn't contain the relaystate parameter. */
        redirect_to = apr_pstrcat(
            r->pool, redirect_to, "&RelayState=",
            am_urlencode(r->pool, LASSO_PROFILE(login)->msg_relayState),
            NULL
            );
    }

    apr_table_setn(r->headers_out, "Location", redirect_to);

    lasso_login_destroy(login);

    /* We don't want to include POST data (in case this was a POST request). */
    return HTTP_SEE_OTHER;
}


static int am_auth_new_ticket(request_rec *r)
{
    am_dir_cfg_rec *cfg = am_get_dir_cfg(r);
    const char *relay_state;

    relay_state = am_reconstruct_url(r);

    /* If this is a POST request, attempt to save it */
    if (r->method_number == M_POST) {
        if (am_save_post(r, &relay_state) != OK)
            return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* Check if IdP discovery is in use and no IdP was selected yet */
    if ((cfg->discovery_url != NULL) &&
        (am_extract_query_parameter(r->pool, r->args, "IdP") == NULL)) {
        char *discovery_url;
        char *return_url;
        char *endpoint = am_get_endpoint_url(r);
        char *sep;

        /* If discovery URL already has a ? we append a & */
        sep = (strchr(cfg->discovery_url, '?')) ? "&" : "?";

        return_url = apr_psprintf(r->pool, "%sauth?ReturnTo=%s",
                                  endpoint,
                                  am_urlencode(r->pool, relay_state));
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "return_url = %s", return_url);
        discovery_url = apr_psprintf(r->pool, "%s%sentityID=%smetadata&"
                                     "return=%s&returnIDParam=IdP",
                                     cfg->discovery_url, sep,
                                     am_urlencode(r->pool, endpoint),
                                     am_urlencode(r->pool, return_url));

        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "discovery_url = %s", discovery_url);
        apr_table_setn(r->headers_out, "Location", discovery_url);
        return HTTP_SEE_OTHER;
    }

    /* If IdP discovery is in use and we have an IdP selected,
     * set the relay_state
     */
    if (cfg->discovery_url != NULL) {
        char *return_url;

        return_url = am_extract_query_parameter(r->pool, r->args, "ReturnTo");
        if ((return_url != NULL) && am_urldecode((char *)return_url) == 0)
            relay_state = return_url;
    }

    return am_send_authn_request(r, am_get_idp(r), relay_state, FALSE);
}

/* This function handles requests to the login handler.
 *
 * Parameters:
 *  request_rec *r       The request.
 *
 * Returns:
 *  OK on success, or an error if any of the steps fail.
 */
static int am_handle_login(request_rec *r)
{
    const char *idp;
    char *return_to;
    char *is_passive_str;
    int is_passive;
    int ret;

    return_to = am_extract_query_parameter(r->pool, r->args, "ReturnTo");
    if(return_to == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Missing required ReturnTo parameter.");
        return HTTP_BAD_REQUEST;
    }

    ret = am_urldecode(return_to);
    if(ret != OK) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Error urldecoding ReturnTo parameter.");
        return ret;
    }

    idp = am_extract_query_parameter(r->pool, r->args, "IdP");
    if(idp != NULL) {
        ret = am_urldecode((char*)idp);
        if(ret != OK) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "Error urldecoding IdP parameter.");
            return ret;
        }
    } else {
        /* Use the default IdP. */
        idp = am_get_idp(r);
    }

    is_passive_str = am_extract_query_parameter(r->pool, r->args, "IsPassive");
    if(is_passive_str != NULL) {
        ret = am_urldecode((char*)is_passive_str);
        if(ret != OK) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "Error urldecoding IsPassive parameter.");
            return ret;
        }
        if(!strcmp(is_passive_str, "true")) {
            is_passive = TRUE;
        } else if(!strcmp(is_passive_str, "false")) {
            is_passive = FALSE;
        } else {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                          "Invalid value for IsPassive parameter - must be \"true\" or \"false\".");
            return HTTP_BAD_REQUEST;
        }
    } else {
        is_passive = FALSE;
    }

    return am_send_authn_request(r, idp, return_to, is_passive);
}

/* This function handles requests to the probe discovery handler
 *
 * Parameters:
 *  request_rec *r       The request.
 *
 * Returns:
 *  OK on success, or an error if any of the steps fail.
 */
static int am_handle_probe_discovery(request_rec *r) {
    am_dir_cfg_rec *cfg = am_get_dir_cfg(r);
    LassoServer *server;
    const char *idp = NULL;
    int timeout;
    GHashTableIter iter;
    char *return_to;
    char *idp_param;
    char *redirect_url;
    int ret;

    server = am_get_lasso_server(r);
    if(server == NULL) {
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /*
     * If built-in IdP discovery is not configured, return error.
     * For now we only have the get-metadata metadata method, so this
     * information is not saved in configuration nor it is checked here.
     */
    timeout = cfg->probe_discovery_timeout;
    if (timeout == -1) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "probe discovery handler invoked but not "
                      "configured. Plase set MellonProbeDiscoveryTimeout.");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /*
     * Check for mandatory arguments early to avoid sending 
     * probles for nothing.
     */
    return_to = am_extract_query_parameter(r->pool, r->args, "return");
    if(return_to == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Missing required return parameter.");
        return HTTP_BAD_REQUEST;
    }

    ret = am_urldecode(return_to);
    if (ret != OK) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, ret, r,
                      "Could not urldecode return value.");
        return HTTP_BAD_REQUEST;
    }

    idp_param = am_extract_query_parameter(r->pool, r->args, "returnIDParam");
    if(idp_param == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                      "Missing required returnIDParam parameter.");
        return HTTP_BAD_REQUEST;
    }

    ret = am_urldecode(idp_param);
    if (ret != OK) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, ret, r,
                      "Could not urldecode returnIDParam value.");
        return HTTP_BAD_REQUEST;
    }

    /*
     * Proceed with built-in IdP discovery. 
     *
     * Send probes for all configured IdP to check availability.
     * The first to answer is chosen, but the list of usable
     * IdP can be restricted in configuration.
     */
    g_hash_table_iter_init(&iter, server->providers);
    while (g_hash_table_iter_next(&iter, (void**)&idp, NULL)) {
        void *dontcare;
        const char *ping_url;
        apr_size_t len;
        long status;

        ping_url = idp;

        /*
         * If a list of IdP was given for probe discovery, 
         * skip any IdP that does not match.
         */
        if (apr_hash_count(cfg->probe_discovery_idp) != 0) {
            char *value = apr_hash_get(cfg->probe_discovery_idp,
                                       idp, APR_HASH_KEY_STRING);

            if (value == NULL) {
                /* idp not in list, try the next one */
                continue;
            } else {
                /* idp in list, use the value as the ping url */
                ping_url = value;
            }
        }

        status = 0;
        if (am_httpclient_get(r, ping_url, &dontcare, &len, 
                              timeout, &status) != OK)
            continue;

        if (status != HTTP_OK) {
	    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
			  "Cannot probe %s: \"%s\" returned HTTP %ld",
			  idp, ping_url, status);
            continue;
        }

        /* We got some succes */
        ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r,
                      "probeDiscovery using %s", idp);
        break;
    }

    /* 
     * On failure, try default
     */
    if (idp == NULL) {
        idp = am_first_idp(r);
        if (idp == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, 
                          "probeDiscovery found no usable IdP.");
            return HTTP_INTERNAL_SERVER_ERROR;
        } else {
            ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "probeDiscovery "
                          "failed, trying default IdP %s", idp); 
        }
    }

    redirect_url = apr_psprintf(r->pool, "%s%s%s=%s", return_to, 
                                strchr(return_to, '?') ? "&" : "?",
                                am_urlencode(r->pool, idp_param), 
                                am_urlencode(r->pool, idp));

    apr_table_setn(r->headers_out, "Location", redirect_url);

    return HTTP_SEE_OTHER;
}


/* This function takes a request for an endpoint and passes it on to the
 * correct handler function.
 *
 * Parameters:
 *  request_rec *r       The request we are currently handling.
 *
 * Returns:
 *  The return value of the endpoint handler function,
 *  or HTTP_NOT_FOUND if we don't have a handler for the requested
 *  endpoint.
 */
static int am_endpoint_handler(request_rec *r)
{
    const char *endpoint;
    am_dir_cfg_rec *dir = am_get_dir_cfg(r);

    /* r->uri starts with cfg->endpoint_path, so we can find the endpoint
     * by extracting the string following chf->endpoint_path.
     */
    endpoint = &r->uri[strlen(dir->endpoint_path)];


    if(!strcmp(endpoint, "postResponse")) {
	return am_handle_post_reply(r);
    } else if(!strcmp(endpoint, "artifactResponse")) {
        return am_handle_artifact_reply(r);
    } else if(!strcmp(endpoint, "auth")) {
        return am_auth_new_ticket(r);
    } else if(!strcmp(endpoint, "metadata")) {
        return OK;
    } else if(!strcmp(endpoint, "repost")) {
        return OK;
    } else if(!strcmp(endpoint, "logout")
              || !strcmp(endpoint, "logoutRequest")) {
        /* logoutRequest is included for backwards-compatibility
         * with version 0.0.6 and older.
         */
        return am_handle_logout(r);
    } else if(!strcmp(endpoint, "login")) {
        return am_handle_login(r);
    } else if(!strcmp(endpoint, "probeDisco")) {
        return am_handle_probe_discovery(r);
    } else {
	ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
		      "Endpoint \"%s\" not handled by mod_auth_mellon.",
		      endpoint);

	return HTTP_NOT_FOUND;
    }
    
}



int am_auth_mellon_user(request_rec *r)
{
    am_dir_cfg_rec *dir = am_get_dir_cfg(r);
    int return_code = HTTP_UNAUTHORIZED;
    am_cache_entry_t *session;

    /* check if we are a subrequest.  if we are, then just return OK
     * without any checking since these cannot be injected (heh). */
    if (r->main)
        return OK;

    /* Check that the user has enabled authentication for this directory. */
    if(dir->enable_mellon == am_enable_off
       || dir->enable_mellon == am_enable_default) {
	return DECLINED;
    }

    /* Disable all caching within this location. */
    am_set_nocache(r);

    /* Check if this is a request for one of our endpoints. We check if
     * the uri starts with the path set with the MellonEndpointPath
     * configuration directive.
     */
    if(strstr(r->uri, dir->endpoint_path) == r->uri) {
        return am_endpoint_handler(r);
    }

    /* Get the session of this request. */
    session = am_get_request_session(r);


    if(dir->enable_mellon == am_enable_auth) {
        /* This page requires the user to be authenticated and authorized. */

        if(session == NULL || !session->logged_in) {
            /* We don't have a valid session. */

            if(session) {
                /* Release the session. */
                am_release_request_session(r, session);
            }

            /* Send the user to the authentication page on the IdP. */
            return am_auth_new_ticket(r);
        }

        /* Verify that the user has access to this resource. */
        return_code = am_check_permissions(r, session);
        if(return_code != OK) {
            am_release_request_session(r, session);

            return return_code;
        }


        /* The user has been authenticated, and we can now populate r->user
         * and the r->subprocess_env with values from the session store.
         */
        am_cache_env_populate(r, session);

        /* Release the session. */
        am_release_request_session(r, session);

        return OK;

    } else {
        /* dir->enable_mellon == am_enable_info:
         * We should pass information about the user to the web application
         * if the user is authorized to access this resource.
         * However, we shouldn't attempt to do any access control.
         */

        if(session != NULL
           && session->logged_in
           && am_check_permissions(r, session) == OK) {

            /* The user is authenticated and has access to the resource.
             * Now we populate the environment with information about
             * the user.
             */
            am_cache_env_populate(r, session);
        }

        if(session != NULL) {
            /* Release the session. */
            am_release_request_session(r, session);
        }

        /* We shouldn't really do any access control, so we always return
         * DECLINED.
         */
        return DECLINED;
    }
}


int am_check_uid(request_rec *r)
{
    am_cache_entry_t *session;
    int return_code = HTTP_UNAUTHORIZED;

    /* check if we are a subrequest.  if we are, then just return OK
     * without any checking since these cannot be injected (heh). */
    if (r->main)
        return OK;


    /* Get the session of this request. */
    session = am_get_request_session(r);

    /* If we don't have a session, then we can't authorize the user. */
    if(session == NULL) {
        return HTTP_UNAUTHORIZED;
    }

    /* If the user isn't logged in, then we can't authorize the user. */
    if(!session->logged_in) {
        am_release_request_session(r, session);
        return HTTP_UNAUTHORIZED;
    }

    /* Verify that the user has access to this resource. */
    return_code = am_check_permissions(r, session);
    if(return_code != OK) {
        am_release_request_session(r, session);
        return HTTP_UNAUTHORIZED;
    }

    /* The user has been authenticated, and we can now populate r->user
     *  and the r->subprocess_env with values from the session store.
     */
    am_cache_env_populate(r, session);

    /* Release the session. */
    am_release_request_session(r, session);

    return OK;
}
