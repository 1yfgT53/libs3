/** **************************************************************************
 * general.c
 * 
 * Copyright 2008 Bryan Ischo <bryan@ischo.com>
 * 
 * This file is part of libs3.
 * 
 * libs3 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3 of the License.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of this library and its programs with the
 * OpenSSL library, and distribute linked combinations including the two.
 *
 * libs3 is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License version 3
 * along with libs3, in a file named COPYING.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 ************************************************************************** **/

#include <ctype.h>
#include <openssl/crypto.h>
#define OPENSSL_THREAD_DEFINES
#include <openssl/opensslconf.h>
#ifndef OPENSSL_THREADS
#error "Threading support required in OpenSSL library, but not provided"
#endif
#include <string.h>
#include "request.h"
#include "simplexml.h"
#include "util.h"

typedef struct S3Mutex CRYPTO_dynlock_value;

static struct S3Mutex **pLocksG;

static S3MutexCreateCallback *mutexCreateCallbackG;
static S3MutexLockCallback *mutexLockCallbackG;
static S3MutexUnlockCallback *mutexUnlockCallbackG;
static S3MutexDestroyCallback *mutexDestroyCallbackG;


static void locking_callback(int mode, int index, const char *file, int line)
{
    if (mode & CRYPTO_LOCK) {
        mutex_lock(pLocksG[index]);
    }
    else {
        mutex_unlock(pLocksG[index]);
    }
}


static struct CRYPTO_dynlock_value *dynlock_create(const char *file, int line)
{
    return (struct CRYPTO_dynlock_value *) mutex_create();
}


static void dynlock_lock(int mode, struct CRYPTO_dynlock_value *pLock,
                         const char *file, int line)
{
    if (mode & CRYPTO_LOCK) {
        mutex_lock((struct S3Mutex *) pLock);
    }
    else {
        mutex_unlock((struct S3Mutex *) pLock);
    }
}


static void dynlock_destroy(struct CRYPTO_dynlock_value *pLock,
                            const char *file, int line)
{
    mutex_destroy((struct S3Mutex *) pLock);
}


static void deinitialize_locks()
{
    CRYPTO_set_dynlock_destroy_callback(NULL);
    CRYPTO_set_dynlock_lock_callback(NULL);
    CRYPTO_set_dynlock_create_callback(NULL);
    CRYPTO_set_locking_callback(NULL);
    CRYPTO_set_id_callback(NULL);

    int count = CRYPTO_num_locks();
    for (int i = 0; i < count; i++) {
        mutex_destroy(pLocksG[i]);
    }

    free(pLocksG);
}


struct S3Mutex *mutex_create()
{
    return (mutexCreateCallbackG ? 
            (*mutexCreateCallbackG)() : (struct S3Mutex *) 1);
}


void mutex_lock(struct S3Mutex *mutex)
{
    if (mutexLockCallbackG) {
        (*mutexLockCallbackG)(mutex);
    }
}


void mutex_unlock(struct S3Mutex *mutex)
{
    if (mutexUnlockCallbackG) {
        (*mutexUnlockCallbackG)(mutex);
    }
}


void mutex_destroy(struct S3Mutex *mutex)
{
    if (mutexDestroyCallbackG) {
        (*mutexDestroyCallbackG)(mutex);
    }
}


S3Status S3_initialize(const char *userAgentInfo,
                       S3ThreadSelfCallback *threadSelfCallback,
                       S3MutexCreateCallback *mutexCreateCallback,
                       S3MutexLockCallback *mutexLockCallback,
                       S3MutexUnlockCallback *mutexUnlockCallback,
                       S3MutexDestroyCallback *mutexDestroyCallback)
{
    mutexCreateCallbackG = mutexCreateCallback;
    mutexLockCallbackG = mutexLockCallback;
    mutexUnlockCallbackG = mutexUnlockCallback;
    mutexDestroyCallbackG = mutexDestroyCallback;

    /* As required by the openssl library for thread support */
    int count = CRYPTO_num_locks(), i;
    
    if (!(pLocksG = 
          (struct S3Mutex **) malloc(count * sizeof(struct S3Mutex *)))) {
        return S3StatusOutOfMemory;
    }

    for (i = 0; i < count; i++) {
        if (!(pLocksG[i] = mutex_create())) {
            while (i-- > 0) {
                mutex_destroy(pLocksG[i]);
            }
            return S3StatusFailedToCreateMutex;
        }
    }

    CRYPTO_set_id_callback(threadSelfCallback);
    CRYPTO_set_locking_callback(&locking_callback);
    CRYPTO_set_dynlock_create_callback(dynlock_create);
    CRYPTO_set_dynlock_lock_callback(dynlock_lock);
    CRYPTO_set_dynlock_destroy_callback(dynlock_destroy);

    S3Status status = request_api_initialize(userAgentInfo);
    if (status != S3StatusOK) {
        deinitialize_locks();
        return status;
    }

    return S3StatusOK;
}


void S3_deinitialize()
{
    request_api_deinitialize();

    deinitialize_locks();
}

const char *S3_get_status_name(S3Status status)
{
    switch (status) {
#define handlecase(s)                           \
        case S3Status##s:                       \
            return #s

        handlecase(OK);
        handlecase(InternalError);
        handlecase(OutOfMemory);
        handlecase(Interrupted);
        handlecase(FailedToCreateMutex);
        handlecase(InvalidBucketNameTooLong);
        handlecase(InvalidBucketNameFirstCharacter);
        handlecase(InvalidBucketNameCharacter);
        handlecase(InvalidBucketNameCharacterSequence);
        handlecase(InvalidBucketNameTooShort);
        handlecase(InvalidBucketNameDotQuadNotation);
        handlecase(QueryParamsTooLong);
        handlecase(FailedToInitializeRequest);
        handlecase(MetaDataHeadersTooLong);
        handlecase(BadMetaData);
        handlecase(BadContentType);
        handlecase(ContentTypeTooLong);
        handlecase(BadMD5);
        handlecase(MD5TooLong);
        handlecase(BadCacheControl);
        handlecase(CacheControlTooLong);
        handlecase(BadContentDispositionFilename);
        handlecase(ContentDispositionFilenameTooLong);
        handlecase(BadContentEncoding);
        handlecase(ContentEncodingTooLong);
        handlecase(BadIfMatchETag);
        handlecase(IfMatchETagTooLong);
        handlecase(BadIfNotMatchETag);
        handlecase(IfNotMatchETagTooLong);
        handlecase(HeadersTooLong);
        handlecase(KeyTooLong);
        handlecase(UriTooLong);
        handlecase(XmlParseFailure);
        handlecase(BadAclEmailAddressTooLong);
        handlecase(BadAclUserIdTooLong);
        handlecase(BadAclUserDisplayNameTooLong);
        handlecase(BadAclGroupUriTooLong);
        handlecase(BadAclPermissionTooLong);
        handlecase(TooManyAclGrants);
        handlecase(BadAclGrantee);
        handlecase(BadAclPermission);
        handlecase(AclXmlDocumentTooLarge);
        handlecase(NameLookupError);
        handlecase(FailedToConnect);
        handlecase(ServerFailedVerification);
        handlecase(ConnectionFailed);
        handlecase(AbortedByCallback);
        handlecase(ErrorAccessDenied);
        handlecase(ErrorAccountProblem);
        handlecase(ErrorAmbiguousGrantByEmailAddress);
        handlecase(ErrorBadDigest);
        handlecase(ErrorBucketAlreadyExists);
        handlecase(ErrorBucketAlreadyOwnedByYou);
        handlecase(ErrorBucketNotEmpty);
        handlecase(ErrorCredentialsNotSupported);
        handlecase(ErrorCrossLocationLoggingProhibited);
        handlecase(ErrorEntityTooSmall);
        handlecase(ErrorEntityTooLarge);
        handlecase(ErrorExpiredToken);
        handlecase(ErrorIncompleteBody);
        handlecase(ErrorIncorrectNumberOfFilesInPostRequest);
        handlecase(ErrorInlineDataTooLarge);
        handlecase(ErrorInternalError);
        handlecase(ErrorInvalidAccessKeyId);
        handlecase(ErrorInvalidAddressingHeader);
        handlecase(ErrorInvalidArgument);
        handlecase(ErrorInvalidBucketName);
        handlecase(ErrorInvalidDigest);
        handlecase(ErrorInvalidLocationConstraint);
        handlecase(ErrorInvalidPayer);
        handlecase(ErrorInvalidPolicyDocument);
        handlecase(ErrorInvalidRange);
        handlecase(ErrorInvalidSecurity);
        handlecase(ErrorInvalidSOAPRequest);
        handlecase(ErrorInvalidStorageClass);
        handlecase(ErrorInvalidTargetBucketForLogging);
        handlecase(ErrorInvalidToken);
        handlecase(ErrorInvalidURI);
        handlecase(ErrorKeyTooLong);
        handlecase(ErrorMalformedACLError);
        handlecase(ErrorMalformedXML);
        handlecase(ErrorMaxMessageLengthExceeded);
        handlecase(ErrorMaxPostPreDataLengthExceededError);
        handlecase(ErrorMetadataTooLarge);
        handlecase(ErrorMethodNotAllowed);
        handlecase(ErrorMissingAttachment);
        handlecase(ErrorMissingContentLength);
        handlecase(ErrorMissingSecurityElement);
        handlecase(ErrorMissingSecurityHeader);
        handlecase(ErrorNoLoggingStatusForKey);
        handlecase(ErrorNoSuchBucket);
        handlecase(ErrorNoSuchKey);
        handlecase(ErrorNotImplemented);
        handlecase(ErrorNotSignedUp);
        handlecase(ErrorOperationAborted);
        handlecase(ErrorPermanentRedirect);
        handlecase(ErrorPreconditionFailed);
        handlecase(ErrorRedirect);
        handlecase(ErrorRequestIsNotMultiPartContent);
        handlecase(ErrorRequestTimeout);
        handlecase(ErrorRequestTimeTooSkewed);
        handlecase(ErrorRequestTorrentOfBucketError);
        handlecase(ErrorSignatureDoesNotMatch);
        handlecase(ErrorSlowDown);
        handlecase(ErrorTemporaryRedirect);
        handlecase(ErrorTokenRefreshRequired);
        handlecase(ErrorTooManyBuckets);
        handlecase(ErrorUnexpectedContent);
        handlecase(ErrorUnresolvableGrantByEmailAddress);
        handlecase(ErrorUserKeyMustBeSpecified);
        handlecase(ErrorUnknown);    
        handlecase(HttpErrorMovedTemporarily);
        handlecase(HttpErrorBadRequest);
        handlecase(HttpErrorForbidden);
        handlecase(HttpErrorNotFound);
        handlecase(HttpErrorConflict);
        handlecase(HttpErrorUnknown);
    }

    return "Unknown";
}


S3Status S3_validate_bucket_name(const char *bucketName, S3UriStyle uriStyle)
{
    int virtualHostStyle = (uriStyle == S3UriStyleVirtualHost);
    int len = 0, maxlen = virtualHostStyle ? 63 : 255;
    const char *b = bucketName;

    int hasDot = 0;
    int hasNonDigit = 0;

    while (*b) {
        if (len == maxlen) {
            return S3StatusInvalidBucketNameTooLong;
        }
        else if (isalpha(*b)) {
            len++, b++;
            hasNonDigit = 1;
        }
        else if (isdigit(*b)) {
            len++, b++;
        }
        else if (len == 0) {
            return S3StatusInvalidBucketNameFirstCharacter;
        }
        else if (*b == '_') {
            /* Virtual host style bucket names cannot have underscores */
            if (virtualHostStyle) {
                return S3StatusInvalidBucketNameCharacter;
            }
            len++, b++;
            hasNonDigit = 1;
        }
        else if (*b == '-') {
            /* Virtual host style bucket names cannot have .- */
            if (virtualHostStyle && (b > bucketName) && (*(b - 1) == '.')) {
                return S3StatusInvalidBucketNameCharacterSequence;
            }
            len++, b++;
            hasNonDigit = 1;
        }
        else if (*b == '.') {
            /* Virtual host style bucket names cannot have -. */
            if (virtualHostStyle && (b > bucketName) && (*(b - 1) == '-')) {
                return S3StatusInvalidBucketNameCharacterSequence;
            }
            len++, b++;
            hasDot = 1;
        }
        else {
            return S3StatusInvalidBucketNameCharacter;
        }
    }

    if (len < 3) {
        return S3StatusInvalidBucketNameTooShort;
    }

    /* It's not clear from Amazon's documentation exactly what 'IP address
       style' means.  In its strictest sense, it could mean 'could be a valid
       IP address', which would mean that 255.255.255.255 would be invalid,
       wherase 256.256.256.256 would be valid.  Or it could mean 'has 4 sets
       of digits separated by dots'.  Who knows.  Let's just be really
       conservative here: if it has any dots, and no non-digit characters,
       then we reject it */
    if (hasDot && !hasNonDigit) {
        return S3StatusInvalidBucketNameDotQuadNotation;
    }

    return S3StatusOK;
}


typedef struct ConvertAclData
{
    char *ownerId;
    int ownerIdLen;
    char *ownerDisplayName;
    int ownerDisplayNameLen;
    int *aclGrantCountReturn;
    S3AclGrant *aclGrants;

    string_buffer(emailAddress, S3_MAX_GRANTEE_EMAIL_ADDRESS_SIZE);
    string_buffer(userId, S3_MAX_GRANTEE_USER_ID_SIZE);
    string_buffer(userDisplayName, S3_MAX_GRANTEE_DISPLAY_NAME_SIZE);
    string_buffer(groupUri, 128);
    string_buffer(permission, 32);
} ConvertAclData;


static S3Status convertAclXmlCallback(const char *elementPath,
                                      const char *data, int dataLen,
                                      void *callbackData)
{
    ConvertAclData *caData = (ConvertAclData *) callbackData;

    int fit;

    if (data) {
        if (!strcmp(elementPath, "AccessControlPolicy/Owner/ID")) {
            caData->ownerIdLen += 
                snprintf(&(caData->ownerId[caData->ownerIdLen]),
                         S3_MAX_GRANTEE_USER_ID_SIZE - caData->ownerIdLen - 1,
                         "%.*s", dataLen, data);
            if (caData->ownerIdLen >= S3_MAX_GRANTEE_USER_ID_SIZE) {
                return S3StatusBadAclUserIdTooLong;
            }
        }
        else if (!strcmp(elementPath, "AccessControlPolicy/Owner/"
                         "DisplayName")) {
            caData->ownerDisplayNameLen += 
                snprintf(&(caData->ownerDisplayName
                           [caData->ownerDisplayNameLen]),
                         S3_MAX_GRANTEE_DISPLAY_NAME_SIZE -
                         caData->ownerDisplayNameLen - 1, 
                         "%.*s", dataLen, data);
            if (caData->ownerDisplayNameLen >= 
                S3_MAX_GRANTEE_DISPLAY_NAME_SIZE) {
                return S3StatusBadAclUserDisplayNameTooLong;
            }
        }
        else if (!strcmp(elementPath, 
                    "AccessControlPolicy/AccessControlList/Grant/"
                    "Grantee/EmailAddress")) {
            // AmazonCustomerByEmail
            string_buffer_append(caData->emailAddress, data, dataLen, fit);
            if (!fit) {
                return S3StatusBadAclEmailAddressTooLong;
            }
        }
        else if (!strcmp(elementPath,
                         "AccessControlPolicy/AccessControlList/Grant/"
                         "Grantee/ID")) {
            // CanonicalUser
            string_buffer_append(caData->userId, data, dataLen, fit);
            if (!fit) {
                return S3StatusBadAclUserIdTooLong;
            }
        }
        else if (!strcmp(elementPath,
                         "AccessControlPolicy/AccessControlList/Grant/"
                         "Grantee/DisplayName")) {
            // CanonicalUser
            string_buffer_append(caData->userDisplayName, data, dataLen, fit);
            if (!fit) {
                return S3StatusBadAclUserDisplayNameTooLong;
            }
        }
        else if (!strcmp(elementPath,
                         "AccessControlPolicy/AccessControlList/Grant/"
                         "Grantee/URI")) {
            // Group
            string_buffer_append(caData->groupUri, data, dataLen, fit);
            if (!fit) {
                return S3StatusBadAclGroupUriTooLong;
            }
        }
        else if (!strcmp(elementPath,
                         "AccessControlPolicy/AccessControlList/Grant/"
                         "Permission")) {
            // Permission
            string_buffer_append(caData->permission, data, dataLen, fit);
            if (!fit) {
                return S3StatusBadAclPermissionTooLong;
            }
        }
    }
    else {
        if (!strcmp(elementPath, "AccessControlPolicy/AccessControlList/"
                    "Grant")) {
            // A grant has just been completed; so add the next S3AclGrant
            // based on the values read
            if (*(caData->aclGrantCountReturn) == S3_MAX_ACL_GRANT_COUNT) {
                return S3StatusTooManyAclGrants;
            }

            S3AclGrant *grant = &(caData->aclGrants
                                  [*(caData->aclGrantCountReturn)]);

            if (caData->emailAddress[0]) {
                grant->granteeType = S3GranteeTypeAmazonCustomerByEmail;
                strcpy(grant->grantee.amazonCustomerByEmail.emailAddress,
                       caData->emailAddress);
            }
            else if (caData->userId[0] && caData->userDisplayName[0]) {
                grant->granteeType = S3GranteeTypeCanonicalUser;
                strcpy(grant->grantee.canonicalUser.id, caData->userId);
                strcpy(grant->grantee.canonicalUser.displayName, 
                       caData->userDisplayName);
            }
            else if (caData->groupUri[0]) {
                if (!strcmp(caData->groupUri,
                            "http://acs.amazonaws.com/groups/global/"
                            "AuthenticatedUsers")) {
                    grant->granteeType = S3GranteeTypeAllAwsUsers;
                }
                else if (!strcmp(caData->groupUri,
                                 "http://acs.amazonaws.com/groups/global/"
                                 "AllUsers")) {
                    grant->granteeType = S3GranteeTypeAllUsers;
                }
                else {
                    return S3StatusBadAclGrantee;
                }
            }
            else {
                return S3StatusBadAclGrantee;
            }

            if (!strcmp(caData->permission, "READ")) {
                grant->permission = S3PermissionRead;
            }
            else if (!strcmp(caData->permission, "WRITE")) {
                grant->permission = S3PermissionWrite;
            }
            else if (!strcmp(caData->permission, "READ_ACP")) {
                grant->permission = S3PermissionReadACP;
            }
            else if (!strcmp(caData->permission, "WRITE_ACP")) {
                grant->permission = S3PermissionWriteACP;
            }
            else if (!strcmp(caData->permission, "FULL_CONTROL")) {
                grant->permission = S3PermissionFullControl;
            }
            else {
                return S3StatusBadAclPermission;
            }

            (*(caData->aclGrantCountReturn))++;

            string_buffer_initialize(caData->emailAddress);
            string_buffer_initialize(caData->userId);
            string_buffer_initialize(caData->userDisplayName);
            string_buffer_initialize(caData->groupUri);
            string_buffer_initialize(caData->permission);
        }
    }

    return S3StatusOK;
}


S3Status S3_convert_acl(char *aclXml, char *ownerId, char *ownerDisplayName,
                        int *aclGrantCountReturn, S3AclGrant *aclGrants)
{
    ConvertAclData data;

    data.ownerId = ownerId;
    data.ownerIdLen = 0;
    data.ownerId[0] = 0;
    data.ownerDisplayName = ownerDisplayName;
    data.ownerDisplayNameLen = 0;
    data.ownerDisplayName[0] = 0;
    data.aclGrantCountReturn = aclGrantCountReturn;
    data.aclGrants = aclGrants;
    *aclGrantCountReturn = 0;
    string_buffer_initialize(data.emailAddress);
    string_buffer_initialize(data.userId);
    string_buffer_initialize(data.userDisplayName);
    string_buffer_initialize(data.groupUri);
    string_buffer_initialize(data.permission);

    // Use a simplexml parser
    SimpleXml simpleXml;
    simplexml_initialize(&simpleXml, &convertAclXmlCallback, &data);

    S3Status status = simplexml_add(&simpleXml, aclXml, strlen(aclXml));

    simplexml_deinitialize(&simpleXml);
                                          
    return status;
}


int S3_status_is_retryable(S3Status status)
{
    switch (status) {
    case S3StatusNameLookupError:
    case S3StatusFailedToConnect:
    case S3StatusConnectionFailed:
    case S3StatusErrorInternalError:
    case S3StatusErrorOperationAborted:
    case S3StatusErrorRequestTimeout:
        return 1;
    default:
        return 0;
    }
}

