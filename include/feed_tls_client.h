#pragma once
#include <WiFiClientSecure.h>
#include <mbedtls/ssl.h>

// Uses the pinned Arduino core's deferred handshake to configure a compact
// TLS 1.2 offer before any TLS/application bytes are sent. Verification and
// hostname/SNI checks remain enabled in NetworkClientSecure.
class FeedTLSClient : public NetworkClientSecure {
public:
    using NetworkClientSecure::connect;
    int connect(const char *host, uint16_t port, int32_t timeout) override {
        static const char *protocols[]={"http/1.1",nullptr};
        static const int suites[]={
            MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
            MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
            MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
            MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384, 0
        };
        _timeout=timeout;
        setAlpnProtocols(protocols);
        setPlainStart();
        // The base implementation resolves DNS, opens TCP, installs the CA and
        // sets the hostname, but defers the handshake while PlainStart is set.
        if(!NetworkClientSecure::connect(host,port,_CA_cert,_cert,_private_key)) return 0;
        mbedtls_ssl_conf_ciphersuites(&sslclient->ssl_conf,suites);
        _stillinPlainStart=false;
        const uint32_t started=millis();
        const int result=ssl_starttls_handshake(sslclient.get());
        sslclient->last_error=result;
        if(result<0) {
            Serial.printf("[tls] %s: error=%d, stage=%d, elapsed=%lu ms\n",host,result,
                          sslclient->ssl_ctx.MBEDTLS_PRIVATE(state),(unsigned long)(millis()-started));
            stop(); return 0;
        }
        Serial.printf("[tls] %s: verified %s / %s\n",host,
                      mbedtls_ssl_get_version(&sslclient->ssl_ctx),mbedtls_ssl_get_ciphersuite(&sslclient->ssl_ctx));
        return 1;
    }
};
