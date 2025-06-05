#define VERBOSE
#define DEBUG

#ifdef VERBOSE
  #ifdef DEBUG
    #define dbg(format, arg...) {printf("%s:%d " format , __FUNCTION__ , __LINE__ , ## arg);}
    #define err(format, arg...) {printf("%s:%d " format , __FUNCTION__ , __LINE__ , ## arg);}
    #define info(format, arg...) {printf("%s:%d " format , __FUNCTION__ , __LINE__ , ## arg);}
    #define warn(format, arg...) {printf("%s:%d " format , __FUNCTION__ , __LINE__ , ## arg);}
  #else
    #define dbg(format, arg...) do {} while (0)
    #define err(format, arg...) {printf("%s:%d " format , __FUNCTION__ , __LINE__ , ## arg);}
    #define info(format, arg...) {printf("%s:%d " format , __FUNCTION__ , __LINE__ , ## arg);}
    #define warn(format, arg...) {printf("%s:%d " format , __FUNCTION__ , __LINE__ , ## arg);}
  #endif
#else
  #define dbg(format, arg...) do {} while (0)
  #define err(format, arg...) do {} while (0)
  #define info(format, arg...) do {} while (0)
  #define warn(format, arg...) do {} while (0)
#endif
#define DEBUG_PRINTF dbg