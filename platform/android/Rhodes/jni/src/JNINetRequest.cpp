/*------------------------------------------------------------------------
* (The MIT License)
* 
* Copyright (c) 2008-2011 Rhomobile, Inc.
* 
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
* 
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
* 
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
* 
* http://rhomobile.com
*------------------------------------------------------------------------*/

#include "rhodes/JNINetRequest.h"

#include <common/RhodesApp.h>
#include <common/RhoConf.h>
#include <net/URI.h>

#include "rhodes/JNIRhodes.h"

#include <algorithm>

#undef DEFAULT_LOGCATEGORY
#define DEFAULT_LOGCATEGORY "Net"

extern "C" void rho_net_impl_network_indicator(int active);

IMPLEMENT_LOGCLASS(rho::net::JNINetRequest, "Net");

class JNINetResponseImpl : public rho::net::INetResponse
{
    rho::String m_data;
    int   m_nRespCode;
    rho::String m_cookies;
    rho::String m_errorMessage;

public:
    JNINetResponseImpl(char const *data, size_t size, int nRespCode)
            :m_nRespCode(nRespCode)
    {
        m_data.assign(data, size);
    }

    virtual const char* getCharData() const
    {
        return m_data.c_str();
    }

    virtual unsigned int getDataSize() const
    {
        return m_data.size();
    }

    virtual int getRespCode() const
    {
        return m_nRespCode;
    }

    virtual rho::String getCookies() const
    {
        return m_cookies;
    }

    virtual rho::String getErrorMessage() const
    {
        return m_errorMessage;
    }

    virtual void setCharData(const char* szData)
    {
        m_data = szData;
    }

    void setRespCode(int nRespCode)
    {
        m_nRespCode = nRespCode;
    }

    rho::boolean isOK() const
    {
        return m_nRespCode == 200 || m_nRespCode == 206;
    }

    rho::boolean isUnathorized() const
    {
        return m_nRespCode == 401;
    }

    rho::boolean isSuccess() const
    {
        return m_nRespCode > 0 && m_nRespCode < 400;
    }

    rho::boolean isResponseRecieved(){ return m_nRespCode!=-1;}

    void setCharData(const rho::String &data)
    {
        m_data = data;//.assign(data.begin(), data.end());
    }

    void setCookies(rho::String s)
    {
        m_cookies = s;
    }

};

void rho::net::JNINetRequest::ProxySettings::initFromConfig() {
    port = 0;

    if (RHOCONF().isExist("http_proxy_host")) {
        host = RHOCONF().getString("http_proxy_host");

        String strPort;

        if (RHOCONF().isExist("http_proxy_port")) {
            strPort = RHOCONF().getString("http_proxy_port");
            port = atoi(strPort.c_str());
        }

        if (RHOCONF().isExist("http_proxy_login")) {
            username = RHOCONF().getString ("http_proxy_login");
        }

        if (RHOCONF().isExist("http_proxy_password")) {
            password = RHOCONF().getString("http_proxy_password");
        }

        LOG(INFO) + "PROXY: " + host + " PORT: " + strPort + " USERNAME: " + username + " PASSWORD: " + password;
    }
}

rho::net::JNINetRequest::JNINetRequest()
{
    JNIEnv *env = jnienv();    
    if (!env) {   
        RAWLOG_ERROR("JNI init failed: jnienv is null");   
        return;    
    }

    cls = getJNIClass(RHODES_JAVA_CLASS_NETREQUEST);
    if (!cls) 
    {
        RAWLOG_ERROR("Class com/rhomobile/rhodes/NetRequest not found!");
        return;
    }

    clsHashMap = getJNIClass(RHODES_JAVA_CLASS_HASHMAP);
    if (!clsHashMap) return;
    midHashMapConstructor = getJNIClassMethod(env, clsHashMap, "<init>", "()V");
    if (!midHashMapConstructor) return;
    midPut = getJNIClassMethod(env, clsHashMap, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
    if (!midPut) return;

    jmethodID constructorNetRequest = getJNIClassMethod(env, cls, "<init>", "()V");
    if (!constructorNetRequest) 
    {
        RAWLOG_ERROR("Constructor for class com/rhomobile/rhodes/NetRequest not found!");
        return;
    }

    netRequestObject = env->NewObject(cls, constructorNetRequest);
    if(!netRequestObject)
    {
        RAWLOG_ERROR("NetRequest object not created!");
        return;
    }

    jobject _netRequestObject = env->NewGlobalRef(netRequestObject);
    env->DeleteLocalRef(netRequestObject);
    netRequestObject = _netRequestObject;

    midDoPull = getJNIClassMethod(env, cls, 
           "doPull", 
           "("
           "Ljava/lang/String;"
           "Ljava/lang/String;"
           "Ljava/lang/String;"
           "Ljava/lang/String;"
           "Ljava/util/HashMap;"
           "ZJ"
           ")I");

    if(!midDoPull)
    {
        RAWLOG_ERROR("doPull method not found!");
        return;
    }

    timeout = rho_conf_getInt("net_timeout");
    if (timeout == 0)
        timeout = 30; // 30 seconds by default

}

jobject rho::net::JNINetRequest::makeJavaHashMap(const Hashtable<String,String>& table)
{
    JNIEnv *env = jnienv();
    jobject v = env->NewObject(clsHashMap, midHashMapConstructor);
    if (!v) return nullptr;

    for(const auto& row : table)
    {
        jhstring key = rho_cast<jstring>(row.first);
        jhstring value = rho_cast<jstring>(row.second);
        env->CallObjectMethod(v, midPut, key.get(), value.get());
    }

    return v;
}

rho::net::INetResponse* rho::net::JNINetRequest::doRequest(const char *method, const String &strUrl, const String &strBody, IRhoSession *oSession, Hashtable<String, String> *pHeaders) {
    return doPull(method, strUrl, strBody, nullptr, oSession, pHeaders);
}

void rho::net::JNINetRequest::cancel() {

}

rho::net::INetResponse* rho::net::JNINetRequest::pullFile(const rho::String &strUrl, common::CRhoFile &oFile, IRhoSession *oSession, Hashtable<String, rho::String> *pHeaders) {
    return doPull("GET", strUrl, String(), &oFile, oSession, pHeaders);
}

rho::net::INetResponse* rho::net::JNINetRequest::createEmptyNetResponse() {
    return nullptr;
}

rho::net::INetResponse* rho::net::JNINetRequest::pushMultipartData(const rho::String &strUrl, VectorPtr<CMultipartItem *> &arItems, IRhoSession *oSession, Hashtable<rho::String, rho::String> *pHeaders) {
    return nullptr;
}

rho::net::INetResponse* rho::net::JNINetRequest::doPull(const char *method, const rho::String &strUrl, const rho::String &strBody, common::CRhoFile *oFile, IRhoSession *oSession, Hashtable<rho::String, rho::String> *pHeaders) {

    int nRespCode = -1;
    long nStartFrom = 0;
    if (oFile) {
        nStartFrom = oFile->size();
    }

    if( !RHODESAPP().isBaseUrl(strUrl.c_str()) )
    {
        //Log every non localhost requests
        RAWLOG_INFO2("%s request (Pull): %s", method, strUrl.c_str());
        rho_net_impl_network_indicator(1);
    }

    JNIEnv *env = jnienv();

    ProxySettings proxySettings;
    proxySettings.initFromConfig();

    Hashtable<rho::String, rho::String> empty_headers;
    Hashtable<rho::String, rho::String>* _pHeaders = pHeaders ? pHeaders : &empty_headers;
    set_options(strUrl, strBody, oFile, oSession, *_pHeaders);

    jhobject headers = makeJavaHashMap(*_pHeaders);
    jhstring jurl = rho_cast<jstring>(env, strUrl);
    jhstring jmethod = rho_cast<jstring>(env, method);
    jhstring jbody = rho_cast<jstring>(env, strBody);
    jint code = env->CallIntMethod(netRequestObject, midDoPull, jurl.get(), jmethod.get(), jbody.get(), nullptr, headers.get(), false, timeout);
}

void rho::net::JNINetRequest::set_options(const String &strUrl, const String &strBody, rho::common::CRhoFile *oFile, rho::net::IRhoSession *pSession,
                                     rho::Hashtable<rho::String, rho::String> headers) {
    rho::String session;
    if (pSession)
        session = pSession->getSession();

    if (!session.empty()) {
        //RAWTRACE1("Set cookie: %s", session.c_str());
        //curl_easy_setopt(m_curl, CURLOPT_COOKIE, session.c_str());
    }

    if (RHODESAPP().isBaseUrl(strUrl))
        timeout = 10*24*60*60;

    headers.emplace("Expect", "");
    headers.emplace("Connection", "Keep-Alive");

    auto it = headers.find("content-type");
    bool hasContentType = (it != headers.end());

    if (!hasContentType && strBody.length() > 0) {
        rho::String strHeader = "Content-Type";
        if ( pSession)
            headers.emplace(strHeader, pSession->getContentType().c_str());
        else
            headers.emplace(strHeader, "application/x-www-form-urlencoded");
    }

}