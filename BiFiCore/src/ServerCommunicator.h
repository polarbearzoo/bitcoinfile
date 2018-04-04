#ifndef __SERVERCOMMUNICATOR_H__
#define __SERVERCOMMUNICATOR_H__


#include <mutex>
#include <string>


class PoolInfo;
class ReportParams;


class ServerCommunicator
{
public:
    enum Region
        {
            Region_Unknown = 0,
            Region_Domestic = 1,
            Region_Abroad = 2,
            Region_Min = 0,
            Region_Max = 1,
        };

public:
    ServerCommunicator(void);
    ~ServerCommunicator(void);

    bool Initialize(void);
    bool Uninitialize(void);
    bool IsInitialized(void) const;

    bool GetPoolInfo(PoolInfo& oPoolInfo);

    bool Report(const ReportParams& oParams);

private:
    static size_t RecvData(char *ptr, size_t size, size_t nmemb, void *userdat);
    size_t OnRecvData(char *ptr, size_t size, size_t nmemb);

    static size_t RecvDataTmp(char *ptr, size_t size, size_t nmemb, void *userdat);
    size_t OnRecvDataTmp(char *ptr, size_t size, size_t nmemb);

    std::string GetUuid(void);
    ServerCommunicator::Region GetRegion(void);
    bool ReadCfg(void);
    bool ParsePoolInfo(PoolInfo& oPoolInfo, const std::string& strJson);

private:
    bool                            m_bInitialized;
    std::string                     m_strUuid;
    std::string                     m_strRecvData;
    ServerCommunicator::Region      m_eRegion;
    std::recursive_mutex            m_oMutex;
};

#endif // __SERVERCOMMUNICATOR_H__
