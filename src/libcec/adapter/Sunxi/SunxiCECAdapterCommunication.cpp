/*
 * This file is part of the libCEC(R) library.
 *
 * libCEC(R) is Copyright (C) 2011-2013 Pulse-Eight Limited.  All rights reserved.
 * libCEC(R) is an original work, containing original code.
 *
 * libCEC(R) is a trademark of Pulse-Eight Limited.
 * 
 * Sunxi adpater port is Copyright (C) 2016 by Joachim Damm
 * based on IMX adpater port Copyright (C) 2013 by Stephan Rafin
 * 
 * You can redistribute this file and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *
 */

#include "env.h"

#if defined(HAVE_SUNXI_API)
#include "SunxiCECAdapterCommunication.h"

#include "CECTypeUtils.h"
#include "LibCEC.h"
#include <p8-platform/sockets/cdevsocket.h>
#include <p8-platform/util/buffer.h>
#include <p8-platform/util/StdString.h>

/*
 * Ioctl definitions from kernel header
 */
#define HDMICEC_IOC_MAGIC  'H'
#define HDMICEC_IOC_SETLOGICALADDRESS _IOW(HDMICEC_IOC_MAGIC,  1, unsigned char)
#define HDMICEC_IOC_STARTDEVICE _IO(HDMICEC_IOC_MAGIC,  2)
#define HDMICEC_IOC_STOPDEVICE  _IO(HDMICEC_IOC_MAGIC,  3)
#define HDMICEC_IOC_GETPHYADDRESS _IOR(HDMICEC_IOC_MAGIC,  4, unsigned char[4])

#define MAX_CEC_MESSAGE_LEN 17

#define MESSAGE_TYPE_RECEIVE_SUCCESS 1
#define MESSAGE_TYPE_NOACK 2
#define MESSAGE_TYPE_DISCONNECTED 3
#define MESSAGE_TYPE_CONNECTED 4
#define MESSAGE_TYPE_SEND_SUCCESS 5

typedef struct hdmi_cec_event{
  int event_type;
  int msg_len;
  unsigned char msg[MAX_CEC_MESSAGE_LEN];
}hdmi_cec_event;


using namespace std;
using namespace CEC;
using namespace P8PLATFORM;

#include "AdapterMessageQueue.h"

#define LIB_CEC m_callback->GetLib()

// these are defined in nxp private header file
#define CEC_MSG_SUCCESS                 0x00	/*Message transmisson Succeed*/
#define CEC_CSP_OFF_STATE               0x80	/*CSP in Off State*/
#define CEC_BAD_REQ_SERVICE             0x81	/*Bad .req service*/
#define CEC_MSG_FAIL_UNABLE_TO_ACCESS	0x82	/*Message transmisson failed: Unable to access CEC line*/
#define CEC_MSG_FAIL_ARBITRATION_ERROR	0x83	/*Message transmisson failed: Arbitration error*/
#define CEC_MSG_FAIL_BIT_TIMMING_ERROR	0x84	/*Message transmisson failed: Bit timming error*/
#define CEC_MSG_FAIL_DEST_NOT_ACK       0x85	/*Message transmisson failed: Destination Address not aknowledged*/
#define CEC_MSG_FAIL_DATA_NOT_ACK       0x86	/*Message transmisson failed: Databyte not acknowledged*/


CSunxiCECAdapterCommunication::CSunxiCECAdapterCommunication(IAdapterCommunicationCallback *callback) :
    IAdapterCommunication(callback)/*,
    m_bLogicalAddressChanged(false)*/
{ 
  CLockObject lock(m_mutex);

  m_iNextMessage = 0;
  //m_logicalAddresses.Clear();
  m_logicalAddress = CECDEVICE_UNKNOWN;
  m_dev = new CCDevSocket(CEC_SUNXI_PATH);
  //m_dev = new CCDevSocket("/dev/sunxi_hdmi_cec");
}

CSunxiCECAdapterCommunication::~CSunxiCECAdapterCommunication(void)
{
  Close();

  CLockObject lock(m_mutex);
  delete m_dev;
  m_dev = 0;
}

bool CSunxiCECAdapterCommunication::IsOpen(void)
{
  return IsInitialised() && m_dev->IsOpen();
}

bool CSunxiCECAdapterCommunication::Open(uint32_t iTimeoutMs, bool UNUSED(bSkipChecks), bool bStartListening)
{
  if (m_dev->Open(iTimeoutMs))
  {
    if (!bStartListening || CreateThread()) {
      if (m_dev->Ioctl(HDMICEC_IOC_STARTDEVICE, NULL) != 0) {
        LIB_CEC->AddLog(CEC_LOG_ERROR, "%s: Unable to start device\n", __func__);
      }
      return true;
    }
    m_dev->Close();
  }

  return false;
}


void CSunxiCECAdapterCommunication::Close(void)
{
  StopThread(0);
  if (m_dev->Ioctl(HDMICEC_IOC_STOPDEVICE, NULL) != 0) {
    LIB_CEC->AddLog(CEC_LOG_ERROR, "%s: Unable to stop device\n", __func__);
  }
  m_dev->Close();
}


std::string CSunxiCECAdapterCommunication::GetError(void) const
{
  std::string strError(m_strError);
  return strError;
}


cec_adapter_message_state CSunxiCECAdapterCommunication::Write(
  const cec_command &data, bool &UNUSED(bRetry), uint8_t UNUSED(iLineTimeout), bool UNUSED(bIsReply))
{
  //cec_frame frame;
  unsigned char message[MAX_CEC_MESSAGE_LEN];
  int msg_len = 1;
  cec_adapter_message_state rc = ADAPTER_MESSAGE_STATE_ERROR;

  if ((size_t)data.parameters.size + data.opcode_set + 1 > sizeof(message))
  {
    LIB_CEC->AddLog(CEC_LOG_ERROR, "%s: data size too large !", __func__);
    return ADAPTER_MESSAGE_STATE_ERROR;
  }

  message[0] = (data.initiator << 4) | (data.destination & 0x0f);
  if (data.opcode_set)
  {
    message[1] = data.opcode;
    msg_len++;
    memcpy(&message[2], data.parameters.data, data.parameters.size);
    msg_len+=data.parameters.size;
  }

  if (m_dev->Write(message, msg_len) == msg_len)
  {
    rc = ADAPTER_MESSAGE_STATE_SENT_ACKED;
  }
    else
      LIB_CEC->AddLog(CEC_LOG_ERROR, "%s: sent command error !", __func__);

  return rc;
}


uint16_t CSunxiCECAdapterCommunication::GetFirmwareVersion(void)
{
  /* FIXME add ioctl ? */
  return 0;
}


cec_vendor_id CSunxiCECAdapterCommunication::GetVendorId(void)
{
  return CEC_VENDOR_UNKNOWN;
}


uint16_t CSunxiCECAdapterCommunication::GetPhysicalAddress(void)
{
  uint32_t info;

  if (m_dev->Ioctl(HDMICEC_IOC_GETPHYADDRESS, &info) != 0)
  {
    LIB_CEC->AddLog(CEC_LOG_ERROR, "%s: HDMICEC_IOC_GETPHYADDRESS failed !", __func__);
    return CEC_INVALID_PHYSICAL_ADDRESS; 
  }

  return info;
}


cec_logical_addresses CSunxiCECAdapterCommunication::GetLogicalAddresses(void)
{
  cec_logical_addresses addresses;
  addresses.Clear();

  CLockObject lock(m_mutex);
  if ( m_logicalAddress != CECDEVICE_UNKNOWN)
    addresses.Set(m_logicalAddress);

  return addresses;
}


bool CSunxiCECAdapterCommunication::SetLogicalAddresses(const cec_logical_addresses &addresses)
{
  int log_addr = addresses.primary;

  CLockObject lock(m_mutex);
  if (m_logicalAddress == log_addr)
      return true;

  if (m_dev->Ioctl(HDMICEC_IOC_SETLOGICALADDRESS, (void *)log_addr) != 0)
  {
    LIB_CEC->AddLog(CEC_LOG_ERROR, "%s: HDMICEC_IOC_SETLOGICALADDRESS failed !", __func__);
    return false;
  }

  m_logicalAddress = (cec_logical_address)log_addr;
  return true;
}


void *CSunxiCECAdapterCommunication::Process(void)
{
  hdmi_cec_event event;
  int ret;
  cec_logical_address initiator, destination;

  while (!IsStopped())
  {
    ret = m_dev->Read((char *)&event, sizeof(event), 5000);
    if (ret > 0)
    {

      initiator = cec_logical_address(event.msg[0] >> 4);
      destination = cec_logical_address(event.msg[0] & 0x0f);

      //LIB_CEC->AddLog(CEC_LOG_DEBUG, "%s: Read data : type : %d initiator %d dest %d", __func__, event.event_type, initiator, destination);
      if (event.event_type == MESSAGE_TYPE_RECEIVE_SUCCESS)
      /* Message received */
      {
        cec_command cmd;

        cec_command::Format(
          cmd, initiator, destination,
          ( event.msg_len > 1 ) ? cec_opcode(event.msg[1]) : CEC_OPCODE_NONE);

        for( uint8_t i = 2; i < event.msg_len; i++ )
          cmd.parameters.PushBack(event.msg[i]);

        if (!IsStopped())
          m_callback->OnCommandReceived(cmd);
      }
      /* We are not interested in other events */
    } /*else {
      LIB_CEC->AddLog(CEC_LOG_DEBUG, "%s: Read returned %d", __func__, ret);
    }*/

  }

  return 0;
}

#endif	// HAVE_SUNXI_API
