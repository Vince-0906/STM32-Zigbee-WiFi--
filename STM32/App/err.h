#ifndef __ERR_H__
#define __ERR_H__

/* 统一返回码：0=OK，<0=err（规范书 §10 #8）。 */

#define RC_OK             0
#define RC_ERR_PARAM     -1
#define RC_ERR_BUSY      -2
#define RC_ERR_TIMEOUT   -3
#define RC_ERR_NO_SPACE  -4
#define RC_ERR_BAD_FRAME -5
#define RC_ERR_BAD_CRC   -6
#define RC_ERR_BAD_JSON  -7
#define RC_ERR_NV        -8
#define RC_ERR_IO        -9
#define RC_ERR_UNKNOWN  -99

#endif /* __ERR_H__ */
