// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GOOGLE_APIS_GAIA_FAKE_GAIA_H_
#define GOOGLE_APIS_GAIA_FAKE_GAIA_H_

#include <memory>
#include <optional>
#include <set>
#include <string>

#include "base/containers/flat_map.h"
#include "base/functional/callback.h"
#include "google_apis/gaia/gaia_auth_consumer.h"
#include "google_apis/gaia/gaia_id.h"
#include "net/http/http_status_code.h"
#include "url/gurl.h"

namespace base {
class ValueView;
}  // namespace base

namespace net::test_server {
class BasicHttpResponse;
struct HttpRequest;
class HttpResponse;
}  // namespace net::test_server

// This is a test helper that implements a fake GAIA service for use in browser
// tests. It's mainly intended for use with EmbeddedTestServer, for which it can
// be registered as an additional request handler.
class FakeGaia {
 public:
  static GaiaId GetDefaultGaiaId() { return GaiaId("12345"); }

  using ScopeSet = std::set<std::string>;
  using RefreshTokenToDeviceIdMap = std::map<std::string, std::string>;

  // Access token details used for token minting and the token info endpoint.
  struct AccessTokenInfo {
    AccessTokenInfo();
    AccessTokenInfo(const AccessTokenInfo& other);
    ~AccessTokenInfo();

    std::string token;
    std::string issued_to;
    std::string audience;
    GaiaId user_id;
    ScopeSet scopes;
    int expires_in = 3600;
    std::string email;
    // When set to true, any scope set for issue token request matches |this|.
    bool any_scope = false;
    std::string id_token;
  };

  // Server configuration: account cookies and tokens.
  struct Configuration {
    Configuration();
    ~Configuration();

    // Updates params with non-empty values from |params|.
    void Update(const Configuration& params);

    // Values of SID and LSID cookie that are set by /ServiceLoginAuth or its
    // equivalent at the end of the SAML login flow.
    std::string auth_sid_cookie;
    std::string auth_lsid_cookie;

    // auth_code cookie value response for /o/oauth2/programmatic_auth call.
    std::string auth_code;

    // OAuth2 refresh access and id token generated by /oauth2/v4/token call
    // with "...&grant_type=authorization_code".
    std::string refresh_token;
    std::string access_token;
    std::string id_token;

    // Values of SID and LSID cookie generated from multilogin call.
    std::string session_sid_cookie;
    std::string session_lsid_cookie;

    // The e-mail address returned by /ListAccounts.
    std::string email;

    // List of signed out gaia IDs returned by /ListAccounts.
    std::vector<GaiaId> signed_out_gaia_ids;
  };

  struct SyncTrustedVaultKeys {
    SyncTrustedVaultKeys();
    ~SyncTrustedVaultKeys();

    std::vector<uint8_t> encryption_key;
    int encryption_key_version = 0;
    std::vector<std::vector<uint8_t>> trusted_public_keys;
  };

  FakeGaia();

  FakeGaia(const FakeGaia&) = delete;
  FakeGaia& operator=(const FakeGaia&) = delete;

  virtual ~FakeGaia();

  void SetConfigurationHelper(const std::string& email,
                              const std::string& auth_sid_cookie,
                              const std::string& auth_lsid_cookie);

  // Sets the initial value of tokens and cookies.
  void SetConfiguration(const Configuration& params);

  // Updates various params with non-empty values from |params|.
  void UpdateConfiguration(const Configuration& params);

  // Sets the specified |gaia_id| as corresponding to the given |email|
  // address when setting GAIA response headers.  If no mapping is given for
  // an email address, a default GAIA Id is used.
  void MapEmailToGaiaId(const std::string& email, const GaiaId& gaia_id);

  // Adds sync trusted vault keys for |email|.
  void SetSyncTrustedVaultKeys(
      const std::string& email,
      const SyncTrustedVaultKeys& sync_trusted_vault_keys);

  // Initializes HTTP request handlers. Should be called after switches
  // for tweaking GaiaUrls are in place.
  void Initialize();

  // Handles a request and returns a response if the request was recognized as a
  // GAIA request. Note that this respects the switches::kGaiaUrl and friends so
  // that this can used with EmbeddedTestServer::RegisterRequestHandler().
  std::unique_ptr<net::test_server::HttpResponse> HandleRequest(
      const net::test_server::HttpRequest& request);

  // Configures an OAuth2 token that'll be returned when a client requests an
  // access token for the given auth token, which can be a refresh token or an
  // login-scoped access token for the token minting endpoint. Note that the
  // scope and audience requested by the client need to match the token_info.
  void IssueOAuthToken(const std::string& auth_token,
                       const AccessTokenInfo& token_info);

  // Associates an account id with a SAML IdP redirect endpoint. When a
  // /ServiceLoginAuth request comes in for that user, it will be redirected
  // to the associated redirect endpoint.
  void RegisterSamlUser(const std::string& account_id, const GURL& saml_idp);

  // Remove association between given user and their SAML IdP. This simulates a
  // switch from SAML to GAIA.
  void RemoveSamlIdpForUser(const std::string& account_id);

  // Associates a SAML `sso_profile` with a SAML IdP redirect endpoint. When a
  // /samlredirect request comes in for this SSO Profile, it will be redirected
  // to this endpoint.
  void RegisterSamlSsoProfileRedirectUrl(const std::string& sso_profile,
                                         const GURL& saml_redirect_url);

  // Associates a SAML `domain` with a SAML IdP redirect endpoint. When a
  // /samlredirect request comes in for this domain, it will be redirected to
  // this endpoint, unless overridden by sso profile.
  void RegisterSamlDomainRedirectUrl(const std::string& domain,
                                     const GURL& saml_redirect_url);

  void set_issue_oauth_code_cookie(bool value) {
    issue_oauth_code_cookie_ = value;
  }

  // Extracts the parameter named |key| from |query| and places it in |value|.
  // Returns false if no parameter is found.
  static bool GetQueryParameter(const std::string& query,
                                const std::string& key,
                                std::string* value);

  // Returns a device ID associated with a given |refresh_token|.
  std::string GetDeviceIdByRefreshToken(const std::string& refresh_token) const;

  void SetRefreshTokenToDeviceIdMap(
      const RefreshTokenToDeviceIdMap& refresh_token_to_device_id_map);

  const RefreshTokenToDeviceIdMap& refresh_token_to_device_id_map() const {
    return refresh_token_to_device_id_map_;
  }

  // Returns an email that is filled into the the Email field (if any).
  const std::string& prefilled_email() { return prefilled_email_; }

  void SetNextReAuthStatus(
      GaiaAuthConsumer::ReAuthProofTokenStatus next_status) {
    next_reauth_status_ = next_status;
  }

  // If set, HandleEmbeddedSetupChromeos will serve a hidden iframe that points
  // to |frame_src_url|.
  void SetIframeOnEmbeddedSetupChromeosUrl(const GURL& frame_src_url) {
    embedded_setup_chromeos_iframe_url_ = frame_src_url;
  }

  // Configures FakeGaia to answer with HTTP status code |http_status_code| and
  // an |http_response_body| body when |gaia_url| is requested. Only
  // |gaia_url|.path() is relevant for the URL match.
  // To reset, pass |http_status_code| = net::HTTP_OK and |http_response_body| =
  // "".
  void SetFixedResponse(const GURL& gaia_url,
                        net::HttpStatusCode http_status_code,
                        const std::string& http_response_body = "");

  // Returns the is_supervised param from the reauth URL if any.
  const std::string& is_supervised() { return is_supervised_; }

  // Returns the is_device_owner param from the reauth URL if any.
  const std::string& is_device_owner() { return is_device_owner_; }

  // Returns the rart param from the embedded setup URL if any.
  const std::string& reauth_request_token() { return reauth_request_token_; }

  // Returns the pwl param from the embedded setup URL if any.
  const std::string& passwordless_support_level() {
    return passwordless_support_level_;
  }

  // Returns the fake server's URL that browser tests can visit to trigger a
  // RemoveLocalAccount event.
  GURL GetFakeRemoveLocalAccountURL(const GaiaId& gaia_id) const;

  void SetFakeSamlContinueResponse(
      const std::string& fake_saml_continue_response) {
    fake_saml_continue_response_ = fake_saml_continue_response;
  }

 private:
  using AccessTokenInfoMap = std::multimap<std::string, AccessTokenInfo>;
  using EmailToGaiaIdMap = std::map<std::string, GaiaId>;
  using SamlAccountIdpMap = std::map<std::string, GURL>;
  using SamlSsoProfileRedirectUrlMap = std::map<std::string, GURL>;
  using SamlDomainRedirectUrlMap = std::map<std::string, GURL>;
  using EmailToSyncTrustedVaultKeysMap =
      std::map<std::string, SyncTrustedVaultKeys>;

  GaiaId GetGaiaIdOfEmail(const std::string& email) const;
  std::string GetEmailOfGaiaId(const GaiaId& gaia_id) const;

  void AddGoogleAccountsSigninHeader(
      net::test_server::BasicHttpResponse* http_response,
      const std::string& email) const;

  void SetOAuthCodeCookie(
      net::test_server::BasicHttpResponse* http_response) const;

  void AddSyncTrustedKeysHeader(
      net::test_server::BasicHttpResponse* http_response,
      const std::string& email) const;

  // Formats a JSON response with the data in |value|, setting the http status
  // to |status|.
  void FormatJSONResponse(const base::ValueView& value,
                          net::HttpStatusCode status,
                          net::test_server::BasicHttpResponse* http_response);

  // Formats a JSON response with the data in |value|, setting the http status
  // to net::HTTP_OK.
  void FormatOkJSONResponse(const base::ValueView& value,
                            net::test_server::BasicHttpResponse* http_response);

  using HttpRequestHandlerCallback = base::RepeatingCallback<void(
      const net::test_server::HttpRequest& request,
      net::test_server::BasicHttpResponse* http_response)>;
  using RequestHandlerMap =
      base::flat_map<std::string, HttpRequestHandlerCallback>;
  using FixedResponseMap =
      base::flat_map<std::string, std::pair<net::HttpStatusCode, std::string>>;

  // Finds the handler for the specified |request_path| by prefix.
  // Used as a backup for situations where an exact match doesn't
  // find a match.
  RequestHandlerMap::iterator FindHandlerByPathPrefix(
      const std::string& request_path);

  // HTTP request handlers.
  void HandleProgramaticAuth(
      const net::test_server::HttpRequest& request,
      net::test_server::BasicHttpResponse* http_response);
  void HandleServiceLogin(const net::test_server::HttpRequest& request,
                          net::test_server::BasicHttpResponse* http_response);
  void HandleEmbeddedSetupChromeos(
      const net::test_server::HttpRequest& request,
      net::test_server::BasicHttpResponse* http_response);
  void HandleEmbeddedReauthChromeos(
      const net::test_server::HttpRequest& request,
      net::test_server::BasicHttpResponse* http_response);
  void HandleEmbeddedLookupAccountLookup(
      const net::test_server::HttpRequest& request,
      net::test_server::BasicHttpResponse* http_response);
  void HandleEmbeddedSigninChallenge(
      const net::test_server::HttpRequest& request,
      net::test_server::BasicHttpResponse* http_response);
  void HandleSSO(const net::test_server::HttpRequest& request,
                 net::test_server::BasicHttpResponse* http_response);
  void HandleFakeSAMLContinue(
      const net::test_server::HttpRequest& request,
      net::test_server::BasicHttpResponse* http_response);
  void HandleAuthToken(const net::test_server::HttpRequest& request,
                       net::test_server::BasicHttpResponse* http_response);
  void HandleTokenInfo(const net::test_server::HttpRequest& request,
                       net::test_server::BasicHttpResponse* http_response);
  void HandleIssueToken(const net::test_server::HttpRequest& request,
                        net::test_server::BasicHttpResponse* http_response);
  void HandleListAccounts(const net::test_server::HttpRequest& request,
                          net::test_server::BasicHttpResponse* http_response);
  void HandlePeopleGet(const net::test_server::HttpRequest& request,
                       net::test_server::BasicHttpResponse* http_response);
  void HandleGetUserInfo(const net::test_server::HttpRequest& request,
                         net::test_server::BasicHttpResponse* http_response);
  void HandleOAuthUserInfo(const net::test_server::HttpRequest& request,
                           net::test_server::BasicHttpResponse* http_response);
  void HandleSAMLRedirect(const net::test_server::HttpRequest& request,
                          net::test_server::BasicHttpResponse* http_response);
  void HandleGetCheckConnectionInfo(
      const net::test_server::HttpRequest& request,
      net::test_server::BasicHttpResponse* http_response);
  void HandleGetReAuthProofToken(
      const net::test_server::HttpRequest& request,
      net::test_server::BasicHttpResponse* http_response);
  // HTTP handler for /OAuth/Multilogin.
  void HandleMultilogin(const net::test_server::HttpRequest& request,
                        net::test_server::BasicHttpResponse* http_response);
  void HandleFakeRemoveLocalAccount(
      const net::test_server::HttpRequest& request,
      net::test_server::BasicHttpResponse* http_response);

  // Returns the access token associated with |auth_token| that matches the
  // given |client_id| and |scope_string|. If |scope_string| is empty, the first
  // token satisfying the other criteria is returned. Returns NULL if no token
  // matches.
  const AccessTokenInfo* FindAccessTokenInfo(
      const std::string& auth_token,
      const std::string& client_id,
      const std::string& scope_string) const;

  // Returns the access token identified by |access_token| or NULL if not found.
  const AccessTokenInfo* GetAccessTokenInfo(
      const std::string& access_token) const;

  // Returns the response content for HandleEmbeddedSetupChromeos, taking into
  // account |embedded_setup_chromeos_iframe_url_| if set.
  std::string GetEmbeddedSetupChromeosResponseContent() const;

  // Returns saml redirect based on given `request_url`. Returns empty object if
  // it fails to determine appropriate redirect url.
  std::optional<GURL> GetSamlRedirectUrl(const GURL& request_url) const;

  Configuration configuration_;
  EmailToGaiaIdMap email_to_gaia_id_map_;
  AccessTokenInfoMap access_token_info_map_;
  RequestHandlerMap request_handlers_;
  FixedResponseMap fixed_responses_;
  std::string embedded_setup_chromeos_response_;
  std::string fake_saml_continue_response_;
  SamlAccountIdpMap saml_account_idp_map_;
  SamlSsoProfileRedirectUrlMap saml_sso_profile_url_map_;
  SamlDomainRedirectUrlMap saml_domain_url_map_;
  bool issue_oauth_code_cookie_;
  RefreshTokenToDeviceIdMap refresh_token_to_device_id_map_;
  EmailToSyncTrustedVaultKeysMap email_to_sync_trusted_vault_keys_map_;
  std::string prefilled_email_;
  std::string is_supervised_;
  std::string is_device_owner_;
  std::string reauth_request_token_;
  std::string passwordless_support_level_;
  GaiaAuthConsumer::ReAuthProofTokenStatus next_reauth_status_ =
      GaiaAuthConsumer::ReAuthProofTokenStatus::kSuccess;
  GURL embedded_setup_chromeos_iframe_url_;
};

#endif  // GOOGLE_APIS_GAIA_FAKE_GAIA_H_
