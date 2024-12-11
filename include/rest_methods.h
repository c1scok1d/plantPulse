#ifndef _REST_METHODS_H
#define _REST_METHODS_H

#ifdef __cplusplus
extern "C"
{
#endif //__cplusplus

    // Use const char* instead of std::string for C compatibility
    int POST(const char* server_uri, const char* to_send);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // _REST_METHODS_H
