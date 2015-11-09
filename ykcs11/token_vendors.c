#include "token_vendors.h"
#include "yubico_token.h"
#include "openssl_utils.h"
#include <string.h>
#include "debug.h"

#include <stdbool.h>
#include "../tool/util.h"

static CK_RV COMMON_token_login(ykpiv_state *state, CK_USER_TYPE user, CK_UTF8CHAR_PTR pin, CK_ULONG pin_len) {

  int tries = 0; // TODO: this is effectively disregarded, should we add a better value in ykpiv_verify?
  unsigned char key[24];
  size_t key_len = sizeof(key);

  if (user == CKU_USER) {
    if (ykpiv_verify(state, (char *)pin, &tries) != YKPIV_OK) {
      DBG("Failed to login");
      return CKR_PIN_INCORRECT;
    }
  }
  else if (user == CKU_SO) {
    if(ykpiv_hex_decode((char *)pin, pin_len, key, &key_len) != YKPIV_OK) {
      DBG("Failed decoding key");
      return CKR_FUNCTION_FAILED;
    }

    if(ykpiv_authenticate(state, key) != YKPIV_OK) {
      DBG("Failed to authenticate");
      return CKR_PIN_INCORRECT;
    }
  }

  return CKR_OK;
}

static CK_RV COMMON_token_generate_key(ykpiv_state *state, CK_BBOOL rsa,
                                       CK_BYTE key, CK_ULONG key_len, CK_ULONG vendor_defined) {
  // TODO: make a function in ykpiv for this
  unsigned char in_data[11];
  unsigned char *in_ptr = in_data;
  unsigned char data[1024];
  unsigned char templ[] = {0, YKPIV_INS_GENERATE_ASYMMERTRIC, 0, 0};
  unsigned char *certptr;
  unsigned long recv_len = sizeof(data);
  int len_bytes;
  int sw;

  CK_RV rv;

  templ[3] = key;

  *in_ptr++ = 0xac;
  *in_ptr++ = 3;
  *in_ptr++ = 0x80;
  *in_ptr++ = 1;

  switch(key_len) {
  case 2048:
    if (rsa == CK_TRUE)
      in_data[4] = YKPIV_ALGO_RSA2048;
    else
      return CKR_FUNCTION_FAILED;

    break;

  case 1024:
    if (rsa == CK_TRUE)
      *in_ptr++ = YKPIV_ALGO_RSA1024;
    else
      return CKR_FUNCTION_FAILED;

    break;

  case 256:
    if (rsa == CK_FALSE)
      *in_ptr++ = YKPIV_ALGO_ECCP256;
    else
      return CKR_FUNCTION_FAILED;

    break;

  default:
    return CKR_FUNCTION_FAILED;
  }
  // PIN policy and touch
  if (vendor_defined != 0) {
    if (vendor_defined & CKA_PIN_ONCE) {
      in_data[1] += 3;
      *in_ptr++ = YKPIV_PINPOLICY_TAG;
      *in_ptr++ = 0x01;
      *in_ptr++ = YKPIV_PINPOLICY_ONCE;
    }
    else if (vendor_defined & CKA_PIN_ALWAYS) {
      in_data[1] += 3;
      *in_ptr++ = YKPIV_PINPOLICY_TAG;
      *in_ptr++ = 0x01;
      *in_ptr++ = YKPIV_PINPOLICY_ALWAYS;
    }

    if (vendor_defined & CKA_TOUCH_ALWAYS) {
      in_data[1] += 3;
      *in_ptr++ = YKPIV_TOUCHPOLICY_TAG;
      *in_ptr++ = 0x01;
      *in_ptr++ = YKPIV_TOUCHPOLICY_ALWAYS;
    }
  }

  if(ykpiv_transfer_data(state, templ, in_data, in_ptr - in_data, data, &recv_len, &sw) != YKPIV_OK ||
     sw != 0x9000)
    return CKR_DEVICE_ERROR;

  // Create a new empty certificate for the key
  recv_len = sizeof(data);
  if ((rv = do_create_empty_cert(data, recv_len, rsa, data, &recv_len)) != CKR_OK)
    return rv;

  if (recv_len < 0x80)
    len_bytes = 1;
  else if (recv_len < 0xff)
    len_bytes = 2;
  else
    len_bytes = 3;

  certptr = data;
  memmove(data + len_bytes + 1, data, recv_len);

  *certptr++ = 0x70;
  certptr += set_length(certptr, recv_len);
  certptr += recv_len;
  *certptr++ = 0x71;
  *certptr++ = 1;
  *certptr++ = 0; /* certinfo (gzip etc) */
  *certptr++ = 0xfe; /* LRC */
  *certptr++ = 0;

  // Store the certificate into the token
  if (ykpiv_save_object(state, key_to_object_id(key), data, (size_t)(certptr - data)) != YKPIV_OK)
    return CKR_DEVICE_ERROR;

  return CKR_OK;
}

static CK_RV COMMON_token_import_cert(ykpiv_state *state, CK_ULONG cert_id, CK_BYTE_PTR in) {

  unsigned char certdata[2100];
  unsigned char *certptr;
  CK_ULONG cert_len;

  CK_RV rv;

  // Check whether or not we have a valid cert
  if ((rv = do_check_cert(in, &cert_len)) != CKR_OK)
    return rv;

  if (cert_len > 2100)
    return CKR_FUNCTION_FAILED;

  certptr = certdata;

  *certptr++ = 0x70;
  certptr += set_length(certptr, cert_len);
  memcpy(certptr, in, cert_len);
  certptr += cert_len;

  *certptr++ = 0x71;
  *certptr++ = 1;
  *certptr++ = 0; /* certinfo (gzip etc) */
  *certptr++ = 0xfe; /* LRC */
  *certptr++ = 0;

  // Store the certificate into the token
  if (ykpiv_save_object(state, cert_id, certdata, (size_t)(certptr - certdata)) != YKPIV_OK)
    return CKR_DEVICE_ERROR;

  return CKR_OK;
}

CK_RV COMMON_token_import_private_key(ykpiv_state *state, CK_BYTE key_id, CK_BYTE_PTR p, CK_BYTE_PTR q,
                                      CK_BYTE_PTR dp, CK_BYTE_PTR dq, CK_BYTE_PTR qinv,
                                      CK_BYTE_PTR ec_data, CK_ULONG elem_len, CK_ULONG vendor_defined) {

  unsigned char key_data[1024];
  unsigned char *in_ptr = key_data;
  unsigned char templ[] = {0, YKPIV_INS_IMPORT_KEY, 0, key_id};
  unsigned char data[0xff];
  unsigned long recv_len = sizeof(data);
  int sw;

  if (elem_len == 128) // TODO: add a flag to check algo type ?
    templ[2] = YKPIV_ALGO_RSA2048;
  else if (elem_len == 64)
    templ[2] = YKPIV_ALGO_RSA1024;
  else if(elem_len == 32)
    templ[2] = YKPIV_ALGO_ECCP256;

  if (templ[2] == YKPIV_ALGO_RSA1024 ||templ[2] == YKPIV_ALGO_RSA2048) {
    *in_ptr++ = 0x01;
    in_ptr += set_length(in_ptr, elem_len);
    memcpy(in_ptr, p, (size_t)(elem_len));
    in_ptr += elem_len;

    *in_ptr++ = 0x02;
    in_ptr += set_length(in_ptr, elem_len);
    memcpy(in_ptr, q, (size_t)(elem_len));
    in_ptr += elem_len;

    *in_ptr++ = 0x03;
    in_ptr += set_length(in_ptr, elem_len);
    memcpy(in_ptr, dp, (size_t)(elem_len));
    in_ptr += elem_len;

    *in_ptr++ = 0x04;
    in_ptr += set_length(in_ptr, elem_len);
    memcpy(in_ptr, dq, (size_t)(elem_len));
    in_ptr += elem_len;

    *in_ptr++ = 0x05;
    in_ptr += set_length(in_ptr, elem_len);
    memcpy(in_ptr, qinv, (size_t)(elem_len));
    in_ptr += elem_len;
  }
  else if (templ[2] == YKPIV_ALGO_ECCP256) {
    *in_ptr++ = 0x06;
    in_ptr += set_length(in_ptr, elem_len);
    memcpy(in_ptr, ec_data, (size_t)(elem_len));
    in_ptr += elem_len;
  }

  // PIN policy and touch
  if (vendor_defined != 0) {
    if (vendor_defined & CKA_PIN_ONCE) {
      *in_ptr++ = YKPIV_PINPOLICY_TAG;
      *in_ptr++ = 0x01;
      *in_ptr++ = YKPIV_PINPOLICY_ONCE;
    }
    else if (vendor_defined & CKA_PIN_ALWAYS) {
      *in_ptr++ = YKPIV_PINPOLICY_TAG;
      *in_ptr++ = 0x01;
      *in_ptr++ = YKPIV_PINPOLICY_ALWAYS;
    }

    if (vendor_defined & CKA_TOUCH_ALWAYS) {
      *in_ptr++ = YKPIV_TOUCHPOLICY_TAG;
      *in_ptr++ = 0x01;
      *in_ptr++ = YKPIV_TOUCHPOLICY_ALWAYS;
    }
  }

  if (ykpiv_transfer_data(state, templ, key_data, in_ptr - key_data, data, &recv_len, &sw) != YKPIV_OK)
    return CKR_FUNCTION_FAILED;

  if (sw != 0x9000)
    return CKR_DEVICE_ERROR;

  return CKR_OK;

}

CK_RV COMMON_token_delete_cert(ykpiv_state *state, CK_ULONG cert_id) {

  if (ykpiv_save_object(state, cert_id, NULL, 0) != YKPIV_OK)
    return CKR_DEVICE_ERROR;

  return CKR_OK;
}

token_vendor_t get_token_vendor(vendor_id_t vid) {
  token_vendor_t v;

  switch (vid) {
  case YUBICO:
    v.get_token_label           = YUBICO_get_token_label;
    v.get_token_manufacturer    = YUBICO_get_token_manufacturer;
    v.get_token_model           = YUBICO_get_token_model;
    v.get_token_flags           = YUBICO_get_token_flags;
    v.get_token_version         = YUBICO_get_token_version;
    v.get_token_serial          = YUBICO_get_token_serial;
    v.get_token_mechanisms_num  = YUBICO_get_token_mechanisms_num;
    v.get_token_mechanism_list  = YUBICO_get_token_mechanism_list;
    v.get_token_mechanism_info  = YUBICO_get_token_mechanism_info;
    v.get_token_objects_num     = YUBICO_get_token_objects_num;
    v.get_token_object_list     = YUBICO_get_token_object_list;
    v.get_token_raw_certificate = YUBICO_get_token_raw_certificate;
    v.token_login               = COMMON_token_login;
    v.token_generate_key        = COMMON_token_generate_key;
    v.token_import_cert         = COMMON_token_import_cert;
    v.token_import_private_key  = COMMON_token_import_private_key;
    v.token_delete_cert         = COMMON_token_delete_cert;
    break;

  case UNKNOWN:
  default:
    v.get_token_label           = NULL;
    v.get_token_manufacturer    = NULL;
    v.get_token_model           = NULL;
    v.get_token_flags           = NULL;
    v.get_token_version         = NULL;
    v.get_token_serial          = NULL;
    v.get_token_mechanisms_num  = NULL;
    v.get_token_mechanism_list  = NULL;
    v.get_token_mechanism_info  = NULL;
    v.get_token_objects_num     = NULL;
    v.get_token_object_list     = NULL;
    v.get_token_raw_certificate = NULL;
    v.token_login               = NULL;
    v.token_generate_key        = NULL;
    v.token_import_cert         = NULL;
    v.token_import_private_key  = NULL;
    v.token_delete_cert         = NULL;
  }

  return v;

}
