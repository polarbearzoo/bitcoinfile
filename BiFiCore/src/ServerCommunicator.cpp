#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <curl/curl.h>
#include <jsoncpp/json/json.h>
#include "ServerCommunicator.h"
#include "PoolInfo.h"
#include "ReportParams.h"
#include "CaBokoClientCert.h"
#include "CaBokoPrivate.h"
#include "CaBopoClientCert.h"
#include "CaBopoPrivate.h"
#include "util.h"


#define GET_ADDRESS_DOMESTIC                "https://boko.baofengcinema.com/api/boko/report/bifi"
#define GET_ADDRESS_ABROAD                  "https://push.bopochain.com/api/report/bifi"

#define CA_FILE_DOMESTIC                    "boko_clientcert.crt"
#define CA_FILE_ABROAD                      "bopo_clientcert.crt"

#define CA_KEYFILE_DOMESTIC                 "boko_private.key"
#define CA_KEYFILE_ABROAD                   "bopo_private.key"

//#define CA_PASSWORD_DOMESTIC                "ulqwqOxl7vheO9dbEUTtC"
const char g_szCaPasswordDomestic[] =\
{\
    0x75,0x6c,0x71,0x77,\
    0x71,0x4f,0x78,0x6c,\
    0x37,0x76,0x68,0x65,\
    0x4f,0x39,0x64,0x62,\
    0x45,0x55,0x54,0x74,\
    0x43,0x00\
};// ulqwqOxl7vheO9dbEUTtC
//#define CA_PASSWORD_ABROAD                  "Pm3zdybiQ0udZpSO6A1RE"
const char g_szCaPasswordAbroad[] =\
{\
    0x50,0x6d,0x33,0x7a,\
    0x64,0x79,0x62,0x69,\
    0x51,0x30,0x75,0x64,\
    0x5a,0x70,0x53,0x4f,\
    0x36,0x41,0x31,0x52,\
    0x45,0x00\
};// Pm3zdybiQ0udZpSO6A1RE

#define JSON_KEY_UUID                       "uuid"
#define JSON_KEY_TIME                       "time"
#define JSON_KEY_VER                        "ver"
#define JSON_KEY_STATUS                     "status"
#define JSON_KEY_MESSAGE                    "message"
#define JSON_KEY_KEY                        "key"
#define JSON_KEY_SIZE                       "size"
#define JSON_KEY_REGION                     "region"

#define STATUS_OK                           (200)

#define CFG_FILE_NAME                       "device.cfg"


namespace
{
    struct HttpsApiInfo
    {
        ServerCommunicator::Region  m_eRegion;
        std::string                 m_strUrl;
        std::string                 m_strCaFilePath;
        std::string                 m_strCaKeyFilePath;
        std::string                 m_strCaPassword;
    };

    bool GetHttpsApiInfo(HttpsApiInfo& stHttpsApiInfo);

    std::string GetCaInfoDir(void);

    void GenerateCaInfo(const HttpsApiInfo& stHttpsApiInfo);
    void EraseCaInfo(const HttpsApiInfo& stHttpsApiInfo);
}


ServerCommunicator::ServerCommunicator(void)
                  : m_bInitialized(false)
                  , m_strUuid()
                  , m_strRecvData()
                  , m_eRegion(Region_Unknown)
                  , m_oMutex()
{
}

ServerCommunicator::~ServerCommunicator(void)
{
}

bool ServerCommunicator::Initialize(void)
{
    std::lock_guard<std::recursive_mutex> oLock(m_oMutex);

    if (IsInitialized())
        {return true;}

    CURLcode eResult = curl_global_init(CURL_GLOBAL_DEFAULT);
    m_bInitialized = (CURLE_OK == eResult);

    return m_bInitialized;
}

bool ServerCommunicator::Uninitialize(void)
{
    std::lock_guard<std::recursive_mutex> oLock(m_oMutex);
    if (!IsInitialized())
        {return true;}

    curl_global_cleanup();
    m_bInitialized = false;

    return true;
}

bool ServerCommunicator::IsInitialized(void) const
{
    return m_bInitialized;
}

bool ServerCommunicator::GetPoolInfo(PoolInfo& oPoolInfo)
{
    std::lock_guard<std::recursive_mutex> oLock(m_oMutex);
    m_strRecvData.erase();

    if (!IsInitialized())
        {return false;}

    std::string strUuid(GetUuid());
    if (strUuid.empty())
        {return false;}
    Region eRegion = GetRegion();
    if (Region_Unknown == eRegion)
        {return false;}

    CURL* pCurl = curl_easy_init();
    if (!pCurl)
        {return false;}

    Json::Value oJsonRoot;
    oJsonRoot[JSON_KEY_UUID]    = strUuid;
    oJsonRoot[JSON_KEY_TIME]    = static_cast<int>(time(nullptr));
    oJsonRoot[JSON_KEY_VER]     = "1.0.0";
    Json::FastWriter writer;
    std::string strJson = writer.write(oJsonRoot);

    bool bResult = false;
    curl_slist* pList = curl_slist_append(nullptr, "Content-Type:application/json;charset=UTF-8");
    curl_easy_setopt(pCurl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(pCurl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(pCurl, CURLOPT_TIMEOUT, 30);
    curl_easy_setopt(pCurl, CURLOPT_SSLCERTTYPE, "PEM");
    curl_easy_setopt(pCurl, CURLOPT_SSLKEYTYPE, "PEM");
    curl_easy_setopt(pCurl, CURLOPT_POST, 1L);
    curl_easy_setopt(pCurl, CURLOPT_POSTFIELDS, strJson.c_str());
    //curl_easy_setopt(pCurl, CURLOPT_POSTFIELDSIZE, strJson.length());
    curl_easy_setopt(pCurl, CURLOPT_HTTPHEADER, pList);
    curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, ServerCommunicator::RecvData);
    curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, this);

    HttpsApiInfo stHttpsApiInfo;
    stHttpsApiInfo.m_eRegion = eRegion;
    if (!GetHttpsApiInfo(stHttpsApiInfo))
    {
        curl_easy_cleanup(pCurl);
        pCurl = nullptr;
        return false;
    }
    curl_easy_setopt(pCurl, CURLOPT_URL, stHttpsApiInfo.m_strUrl.c_str());
    curl_easy_setopt(pCurl, CURLOPT_SSLCERT, stHttpsApiInfo.m_strCaFilePath.c_str());
    curl_easy_setopt(pCurl, CURLOPT_SSLKEY, stHttpsApiInfo.m_strCaKeyFilePath.c_str());
    curl_easy_setopt(pCurl, CURLOPT_KEYPASSWD, stHttpsApiInfo.m_strCaPassword.c_str());

    GenerateCaInfo(stHttpsApiInfo);

    CURLcode eResult = curl_easy_perform(pCurl);
    bResult = (CURLE_OK == eResult);
    if (!bResult)
    {
        curl_easy_strerror(eResult);
    }

    EraseCaInfo(stHttpsApiInfo);

    curl_slist_free_all(pList);
    pList = nullptr;

    curl_easy_cleanup(pCurl);
    pCurl = nullptr;

    if (!bResult)
        {return false;}
    return ParsePoolInfo(oPoolInfo, m_strRecvData);
}

bool ServerCommunicator::Report(const ReportParams& oParams)
{
    if (!IsInitialized())
        {return false;}

    ReportParams oParamsCopy(oParams);
    if (oParamsCopy.m_strUuid.empty())
        {oParamsCopy.m_strUuid = GetUuid();}
    std::string strUrl(oParamsCopy.GetReportUrl());
    if (strUrl.empty())
        {return false;}

    CURL* pCurl = curl_easy_init();
    if (!pCurl)
        {return false;}

    curl_easy_setopt(pCurl, CURLOPT_URL, strUrl.c_str());
    curl_easy_setopt(pCurl, CURLOPT_TIMEOUT, 30);
    curl_easy_setopt(pCurl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, ServerCommunicator::RecvDataTmp);
    curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, this);

    CURLcode eResult = curl_easy_perform(pCurl);
    bool bResult = (CURLE_OK == eResult);
    if (!bResult)
    {
        curl_easy_strerror(eResult);
    }

    curl_easy_cleanup(pCurl);
    pCurl = nullptr;

    return bResult;
}

size_t ServerCommunicator::RecvData(char *ptr, size_t size, size_t nmemb, void *userdat)
{
    ServerCommunicator* pThis = reinterpret_cast<ServerCommunicator*>(userdat);
    return pThis->OnRecvData(ptr, size, nmemb);
}

size_t ServerCommunicator::OnRecvData(char *ptr, size_t size, size_t nmemb)
{
    std::lock_guard<std::recursive_mutex> oLock(m_oMutex);
    if (ptr)
    {
        std::string strNewData(ptr, size*nmemb);
        m_strRecvData.append(strNewData);
    }
    return (size*nmemb);
}

size_t ServerCommunicator::RecvDataTmp(char *ptr, size_t size, size_t nmemb, void *userdat)
{
    ServerCommunicator* pThis = reinterpret_cast<ServerCommunicator*>(userdat);
    return pThis->OnRecvDataTmp(ptr, size, nmemb);
}

size_t ServerCommunicator::OnRecvDataTmp(char *ptr, size_t size, size_t nmemb)
{
    std::lock_guard<std::recursive_mutex> oLock(m_oMutex);
    if (ptr)
    {
        std::string strNewData(ptr, size*nmemb);
        m_strRecvData.append(strNewData);
    }
    return (size*nmemb);
}

std::string ServerCommunicator::GetUuid(void)
{
    std::lock_guard<std::recursive_mutex> oLock(m_oMutex);
    if (m_strUuid.empty())
        {ReadCfg();}
    return m_strUuid;
}

ServerCommunicator::Region ServerCommunicator::GetRegion(void)
{
    std::lock_guard<std::recursive_mutex> oLock(m_oMutex);
    if (Region_Unknown == m_eRegion)
        {ReadCfg();}
    return m_eRegion;
}

bool ServerCommunicator::ReadCfg(void)
{
    fs::path oPath(GetDataDir());
    std::string strCfgPath(oPath.string());
    strCfgPath.append("/").append(CFG_FILE_NAME);
    FILE * pFile = fopen(strCfgPath.c_str(), "rb");
    if (!pFile)
        {return false;}
    char szBuf[256] = {0};
    fread(szBuf, 1, sizeof(szBuf)-1, pFile);
    fclose(pFile);
    pFile = nullptr;

    Json::Reader oJsonReader;
    Json::Value oJsonRoot;
    if (!oJsonReader.parse(szBuf, oJsonRoot))
        {return false;}

    std::lock_guard<std::recursive_mutex> oLock(m_oMutex);
    // uuid
    const Json::Value& oJsonUuid = oJsonRoot[JSON_KEY_UUID];
    if (oJsonUuid.isString())
        {m_strUuid = oJsonUuid.asString();}

    // region
    const Json::Value& oJsonRegion = oJsonRoot[JSON_KEY_REGION];
    if (oJsonRegion.isInt())
    {
        int nRegion = oJsonRegion.asInt();
        if ((Region_Min <= nRegion) && (Region_Max <= nRegion))
            {m_eRegion = static_cast<Region>(nRegion);}
    }

    return (!m_strUuid.empty() && (Region_Unknown != m_eRegion));
}

bool ServerCommunicator::ParsePoolInfo(PoolInfo& oPoolInfo, const std::string& strJson)
{
    if (strJson.empty())
        {return false;}

    Json::Reader oJsonReader;
    Json::Value oJsonRootValue;
    if (!oJsonReader.parse(strJson, oJsonRootValue))
        {return false;}

    const Json::Value& oJsonStatus = oJsonRootValue[JSON_KEY_STATUS];
    if (!oJsonStatus.isInt())
        {return false;}
    int nStatus = oJsonStatus.asInt();
    if (STATUS_OK != nStatus)
        {return false;}

//    oJsonRootValue[JSON_KEY_MESSAGE];

    const Json::Value& oJsonKey = oJsonRootValue[JSON_KEY_KEY];
    if (!oJsonKey.isString())
        {return false;}
    oPoolInfo.m_strKey = oJsonKey.asString();

    const Json::Value& oJsonSize = oJsonRootValue[JSON_KEY_SIZE];
    if (!oJsonSize.isInt())
        {return false;}
    oPoolInfo.m_nCapacity = oJsonSize.asInt();

    return (!oPoolInfo.m_strKey.empty() && (0 < oPoolInfo.m_nCapacity));
}


namespace
{
    bool GetHttpsApiInfo(HttpsApiInfo& stHttpsApiInfo)
    {
        std::string strCaInfoDir(GetCaInfoDir());
        if (ServerCommunicator::Region_Domestic == stHttpsApiInfo.m_eRegion)
        {
            stHttpsApiInfo.m_strUrl            = GET_ADDRESS_DOMESTIC;
            stHttpsApiInfo.m_strCaPassword     = g_szCaPasswordDomestic;
            stHttpsApiInfo.m_strCaFilePath     = strCaInfoDir;
            stHttpsApiInfo.m_strCaFilePath.append("/").append(CA_FILE_DOMESTIC);
            stHttpsApiInfo.m_strCaKeyFilePath  = strCaInfoDir;
            stHttpsApiInfo.m_strCaKeyFilePath.append("/").append(CA_KEYFILE_DOMESTIC);
        }
        else if (ServerCommunicator::Region_Abroad == stHttpsApiInfo.m_eRegion)
        {
            stHttpsApiInfo.m_strUrl            = GET_ADDRESS_ABROAD;
            stHttpsApiInfo.m_strCaPassword     = g_szCaPasswordAbroad;
            stHttpsApiInfo.m_strCaFilePath     = strCaInfoDir;
            stHttpsApiInfo.m_strCaFilePath.append("/").append(CA_FILE_ABROAD);
            stHttpsApiInfo.m_strCaKeyFilePath  = strCaInfoDir;
            stHttpsApiInfo.m_strCaKeyFilePath.append("/").append(CA_KEYFILE_ABROAD);
        }
        else
        {
            return false;
        }
        return true;
    }

    std::string GetCaInfoDir(void)
    {
        fs::path oPath(GetDataDir());
        return oPath.string();
    }

    void GenerateCaInfo(const HttpsApiInfo& stHttpsApiInfo)
    {
        if (ServerCommunicator::Region_Domestic == stHttpsApiInfo.m_eRegion)
        {
            FILE* pCertFile = fopen(stHttpsApiInfo.m_strCaFilePath.c_str(), "wb");
            fwrite(g_szBokoClientCertData, sizeof(char), sizeof(g_szBokoClientCertData), pCertFile);
            fclose(pCertFile);
            pCertFile = nullptr;

            FILE* pPrivateFile = fopen(stHttpsApiInfo.m_strCaKeyFilePath.c_str(), "wb");
            fwrite(g_szBokoPrivateData, sizeof(char), sizeof(g_szBokoPrivateData), pPrivateFile);
            fclose(pPrivateFile);
            pPrivateFile = nullptr;
        }
        else if (ServerCommunicator::Region_Abroad == stHttpsApiInfo.m_eRegion)
        {
            FILE* pCertFile = fopen(stHttpsApiInfo.m_strCaFilePath.c_str(), "wb");
            fwrite(g_szBopoClientCertData, sizeof(char), sizeof(g_szBopoClientCertData), pCertFile);
            fclose(pCertFile);
            pCertFile = nullptr;

            FILE* pPrivateFile = fopen(stHttpsApiInfo.m_strCaKeyFilePath.c_str(), "wb");
            fwrite(g_szBopoPrivateData, sizeof(char), sizeof(g_szBopoPrivateData), pPrivateFile);
            fclose(pPrivateFile);
            pPrivateFile = nullptr;
        }
    }

    void EraseCaInfo(const HttpsApiInfo& stHttpsApiInfo)
    {
        remove(stHttpsApiInfo.m_strCaFilePath.c_str());
        remove(stHttpsApiInfo.m_strCaKeyFilePath.c_str());
    }
}
