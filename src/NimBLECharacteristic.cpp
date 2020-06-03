/*
 * NimBLECharacteristic.cpp
 *
 *  Created: on March 3, 2020
 *      Author H2zero
 *
 * BLECharacteristic.cpp
 *
 *  Created on: Jun 22, 2017
 *      Author: kolban
 */
#include "sdkconfig.h"
#if defined(CONFIG_BT_ENABLED)

#include "nimconfig.h"
#if defined(CONFIG_BT_NIMBLE_ROLE_PERIPHERAL)

#include "NimBLECharacteristic.h"
#include "NimBLE2902.h"
#include "NimBLE2904.h"
#include "NimBLEDevice.h"
#include "NimBLEUtils.h"
#include "NimBLELog.h"

#define NULL_HANDLE (0xffff)

static NimBLECharacteristicCallbacks defaultCallback;
static const char* LOG_TAG = "NimBLECharacteristic";

/**
 * @brief Construct a characteristic
 * @param [in] uuid - UUID (const char*) for the characteristic.
 * @param [in] properties - Properties for the characteristic.
 * @param [in] pService - pointer to the service instance this characteristic belongs to.
 */
NimBLECharacteristic::NimBLECharacteristic(const char* uuid, uint16_t properties, NimBLEService* pService)
: NimBLECharacteristic(NimBLEUUID(uuid), properties, pService) {
}

/**
 * @brief Construct a characteristic
 * @param [in] uuid - UUID for the characteristic.
 * @param [in] properties - Properties for the characteristic.
 * @param [in] pService - pointer to the service instance this characteristic belongs to.
 */
NimBLECharacteristic::NimBLECharacteristic(const NimBLEUUID &uuid, uint16_t properties, NimBLEService* pService) {
    m_uuid       = uuid;
    m_handle     = NULL_HANDLE;
    m_properties = properties;
    m_pCallbacks = &defaultCallback;
    m_pService   = pService;
    if(properties & NIMBLE_PROPERTY::INDICATE){
        m_pSemaphore = new FreeRTOS::Semaphore("ConfEvt");
    } else {
        m_pSemaphore = nullptr;
    }
} // NimBLECharacteristic

/**
 * @brief Destructor.
 */
NimBLECharacteristic::~NimBLECharacteristic() {
    if(m_pSemaphore != nullptr) {
        delete(m_pSemaphore);
    }
} // ~NimBLECharacteristic


/**
 * @brief Create a new BLE Descriptor associated with this characteristic.
 * @param [in] uuid - The UUID of the descriptor.
 * @param [in] properties - The properties of the descriptor.
 * @return The new BLE descriptor.
 */
NimBLEDescriptor* NimBLECharacteristic::createDescriptor(const char* uuid, uint32_t properties, uint16_t max_len) {
    return createDescriptor(NimBLEUUID(uuid), properties, max_len);
}


/**
 * @brief Create a new BLE Descriptor associated with this characteristic.
 * @param [in] uuid - The UUID of the descriptor.
 * @param [in] properties - The properties of the descriptor.
 * @return The new BLE descriptor.
 */
NimBLEDescriptor* NimBLECharacteristic::createDescriptor(const NimBLEUUID &uuid, uint32_t properties, uint16_t max_len) {
    NimBLEDescriptor* pDescriptor = nullptr;
    if(uuid == NimBLEUUID(uint16_t(0x2902))) {
        if(!(m_properties & BLE_GATT_CHR_F_NOTIFY) && !(m_properties & BLE_GATT_CHR_F_INDICATE)) {
            assert(0 && "Cannot create 2902 descriptior without characteristic notification or indication property set");
        }
        // We cannot have more than one 2902 descriptor, if it's already been created just return a pointer to it.
        pDescriptor = getDescriptorByUUID(uuid);
        if(pDescriptor == nullptr) {
            pDescriptor = new NimBLE2902(this);
        } else {
            return pDescriptor;
        }

    } else if (uuid == NimBLEUUID(uint16_t(0x2904))) {
        pDescriptor = new NimBLE2904(this);

    } else {
        pDescriptor = new NimBLEDescriptor(uuid, properties, max_len, this);
    }

    m_dscVec.push_back(pDescriptor);
    return pDescriptor;
} // createCharacteristic


/**
 * @brief Return the BLE Descriptor for the given UUID if associated with this characteristic.
 * @param [in] descriptorUUID The UUID of the descriptor that we wish to retrieve.
 * @return The BLE Descriptor.  If no such descriptor is associated with the characteristic, nullptr is returned.
 */
NimBLEDescriptor* NimBLECharacteristic::getDescriptorByUUID(const char* uuid) {
    return getDescriptorByUUID(NimBLEUUID(uuid));
} // getDescriptorByUUID


/**
 * @brief Return the BLE Descriptor for the given UUID if associated with this characteristic.
 * @param [in] descriptorUUID The UUID of the descriptor that we wish to retrieve.
 * @return The BLE Descriptor.  If no such descriptor is associated with the characteristic, nullptr is returned.
 */
NimBLEDescriptor* NimBLECharacteristic::getDescriptorByUUID(const NimBLEUUID &uuid) {
    for (auto &it : m_dscVec) {
        if (it->getUUID() == uuid) {
            return it;
        }
    }
    return nullptr;
} // getDescriptorByUUID


/**
 * @brief Get the handle of the characteristic.
 * @return The handle of the characteristic.
 */
uint16_t NimBLECharacteristic::getHandle() {
    return m_handle;
} // getHandle


uint8_t NimBLECharacteristic::getProperties() {
    return m_properties;
} // getProperties


/**
 * @brief Get the service associated with this characteristic.
 */
NimBLEService* NimBLECharacteristic::getService() {
    return m_pService;
} // getService


/**
 * @brief Get the UUID of the characteristic.
 * @return The UUID of the characteristic.
 */
NimBLEUUID NimBLECharacteristic::getUUID() {
    return m_uuid;
} // getUUID


/**
 * @brief Retrieve the current value of the characteristic.
 * @return A pointer to storage containing the current characteristic value.
 */
std::string NimBLECharacteristic::getValue() {
    return m_value.getValue();
} // getValue


/**
 * @brief Retrieve the current raw data of the characteristic.
 * @return A pointer to storage containing the current characteristic data.
 */
uint8_t* NimBLECharacteristic::getData() {
    return m_value.getData();
} // getData


/**
 * @brief Retrieve the the current data length of the characteristic.
 * @return The length of the current characteristic data.
 */
size_t NimBLECharacteristic:: getDataLength() {
    return m_value.getLength();
}


int NimBLECharacteristic::handleGapEvent(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt,
                             void *arg)
{
    const ble_uuid_t *uuid;
    int rc;
    NimBLECharacteristic* pCharacteristic = (NimBLECharacteristic*)arg;

    NIMBLE_LOGD(LOG_TAG, "Characteristic %s %s event", pCharacteristic->getUUID().toString().c_str(),
                                    ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR ? "Read" : "Write");

    uuid = ctxt->chr->uuid;
    if(ble_uuid_cmp(uuid, &pCharacteristic->getUUID().getNative()->u) == 0){
        switch(ctxt->op) {
            case BLE_GATT_ACCESS_OP_READ_CHR: {
                // If the packet header is only 8 bytes this is a follow up of a long read
                // so we don't want to call the onRead() callback again.
                if(ctxt->om->om_pkthdr_len > 8) {
                    pCharacteristic->m_pCallbacks->onRead(pCharacteristic);
                }
                rc = os_mbuf_append(ctxt->om, pCharacteristic->getData(), pCharacteristic->m_value.getLength());
                return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }

            case BLE_GATT_ACCESS_OP_WRITE_CHR: {
                if (ctxt->om->om_len > BLE_ATT_ATTR_MAX_LEN) {
                    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
                }

                pCharacteristic->m_value.addPart(ctxt->om->om_data, ctxt->om->om_len);

                os_mbuf *next;
                next = SLIST_NEXT(ctxt->om, om_next);
                while(next != NULL){
                    pCharacteristic->m_value.addPart(next->om_data, next->om_len);
                    next = SLIST_NEXT(next, om_next);
                }

                pCharacteristic->m_value.commit();
                pCharacteristic->m_pCallbacks->onWrite(pCharacteristic);

                return 0;
            }
            default:
                break;
        }
    }

    return BLE_ATT_ERR_UNLIKELY;
}


/**
 * @brief Set the subscribe status for this characteristic.
 * This will maintain a map of subscribed clients and their indicate/notify status.
 * @return N/A
 */
void NimBLECharacteristic::setSubscribe(struct ble_gap_event *event) {
    uint16_t subVal = 0;
    if(event->subscribe.cur_notify) {
        subVal |= NIMBLE_DESC_FLAG_NOTIFY;
    }
    if(event->subscribe.cur_indicate) {
        subVal |= NIMBLE_DESC_FLAG_INDICATE;
    }

    if(m_pSemaphore != nullptr) {
        m_pSemaphore->give((subVal & NIMBLE_DESC_FLAG_INDICATE) ? 0 :
                          NimBLECharacteristicCallbacks::Status::ERROR_INDICATE_DISABLED);
    }

    NIMBLE_LOGI(LOG_TAG, "New subscribe value for conn: %d val: %d",
                         event->subscribe.conn_handle, subVal);

    NimBLE2902* p2902 = (NimBLE2902*)getDescriptorByUUID(uint16_t(0x2902));
    if(p2902 == nullptr){
        ESP_LOGE(LOG_TAG, "No 2902 descriptor found for %s",
        std::string(getUUID()).c_str());
        return;
    }

    p2902->setNotifications(subVal & NIMBLE_DESC_FLAG_NOTIFY);
    p2902->setIndications(subVal & NIMBLE_DESC_FLAG_INDICATE);
    p2902->m_pCallbacks->onWrite(p2902);


    auto it = p2902->m_subscribedVec.begin();
    for(;it != p2902->m_subscribedVec.end(); ++it) {
        if((*it).conn_id == event->subscribe.conn_handle) {
            break;
        }
    }

    if(subVal > 0) {
        if(it == p2902->m_subscribedVec.end()) {
            chr_sub_status_t client_sub;
            client_sub.conn_id = event->subscribe.conn_handle;
            client_sub.sub_val = subVal;
            p2902->m_subscribedVec.push_back(client_sub);
            return;
        }

        (*it).sub_val = subVal;

    } else if(it != p2902->m_subscribedVec.end()) {
        p2902->m_subscribedVec.erase(it);
        p2902->m_subscribedVec.shrink_to_fit();
    }
}


/**
 * @brief Send an indication.
 * An indication is a transmission of up to the first 20 bytes of the characteristic value.  An indication
 * will block waiting a positive confirmation from the client.
 * @return N/A
 */
void NimBLECharacteristic::indicate() {
    NIMBLE_LOGD(LOG_TAG, ">> indicate: length: %d", m_value.getValue().length());
    notify(false);
    NIMBLE_LOGD(LOG_TAG, "<< indicate");
} // indicate

/**
 * @brief Send a notify.
 * A notification is a transmission of up to the first 20 bytes of the characteristic value.  An notification
 * will not block; it is a fire and forget.
 * @return N/A.
 */
void NimBLECharacteristic::notify(bool is_notification) {
    NIMBLE_LOGD(LOG_TAG, ">> notify: length: %d", m_value.getValue().length());

    assert(getService() != nullptr);
    assert(getService()->getServer() != nullptr);


    if (getService()->getServer()->getConnectedCount() == 0) {
        NIMBLE_LOGD(LOG_TAG, "<< notify: No connected clients.");
        return;
    }

    m_pCallbacks->onNotify(this);

    int rc = 0;
    NimBLE2902* p2902 = (NimBLE2902*)getDescriptorByUUID(uint16_t(0x2902));

    for (auto &it : p2902->m_subscribedVec) {
        uint16_t _mtu = getService()->getServer()->getPeerMTU(it.conn_id);
        // Must rebuild the data on each loop iteration as NimBLE will release it.
        size_t length = m_value.getValue().length();
        uint8_t* data = (uint8_t*)m_value.getValue().data();
        os_mbuf *om;

        if(_mtu == 0) {
            //NIMBLE_LOGD(LOG_TAG, "peer not connected");
            continue;
        }

        if(it.sub_val == 0) {
            //NIMBLE_LOGD(LOG_TAG, "Skipping unsubscribed client");
            continue;
        }

        if (length > _mtu - 3) {
            NIMBLE_LOGW(LOG_TAG, "- Truncating to %d bytes (maximum notify size)", _mtu - 3);
        }

        if(is_notification && (!(it.sub_val & NIMBLE_DESC_FLAG_NOTIFY))) {
            NIMBLE_LOGW(LOG_TAG,
            "Sending notification to client subscribed to indications, sending indication instead");
            is_notification = false;
        }

        if(!is_notification && (!(it.sub_val & NIMBLE_DESC_FLAG_INDICATE))) {
            NIMBLE_LOGW(LOG_TAG,
            "Sending indication to client subscribed to notification, sending notification instead");
            is_notification = true;
        }

        // don't create the m_buf until we are sure to send the data or else
        // we could be allocating a buffer that doesn't get released.
        // We also must create it in each loop iteration because it is consumed with each host call.
        om = ble_hs_mbuf_from_flat(data, length);

        NimBLECharacteristicCallbacks::Status statusRC;
        if(m_pSemaphore != nullptr && !is_notification) {
            m_pSemaphore->take("indicate");
            rc = ble_gattc_indicate_custom(it.conn_id, m_handle, om);
            if(rc != 0){
                m_pSemaphore->give();
                statusRC = NimBLECharacteristicCallbacks::Status::ERROR_GATT;
            } else {
                rc = m_pSemaphore->wait();
            }

            if(rc == 0 || rc == BLE_HS_EDONE) {
                rc = 0;
                statusRC = NimBLECharacteristicCallbacks::Status::SUCCESS_INDICATE;
            } else if(rc == BLE_HS_ETIMEOUT) {
                statusRC = NimBLECharacteristicCallbacks::Status::ERROR_INDICATE_TIMEOUT;
            } else {
                statusRC = NimBLECharacteristicCallbacks::Status::ERROR_INDICATE_FAILURE;
            }
        } else {
            rc = ble_gattc_notify_custom(it.conn_id, m_handle, om);
            if(rc == 0) {
                statusRC = NimBLECharacteristicCallbacks::Status::SUCCESS_NOTIFY;
            } else {
                statusRC = NimBLECharacteristicCallbacks::Status::ERROR_GATT;
            }
        }

        m_pCallbacks->onStatus(this, statusRC, rc);
    }

    NIMBLE_LOGD(LOG_TAG, "<< notify");
} // Notify


/**
 * @brief Set the callback handlers for this characteristic.
 * @param [in] pCallbacks An instance of a callbacks structure used to define any callbacks for the characteristic.
 */
void NimBLECharacteristic::setCallbacks(NimBLECharacteristicCallbacks* pCallbacks) {
    if (pCallbacks != nullptr){
        m_pCallbacks = pCallbacks;
    } else {
        m_pCallbacks = &defaultCallback;
    }
} // setCallbacks


/**
 * @brief Set the value of the characteristic.
 * @param [in] data The data to set for the characteristic.
 * @param [in] length The length of the data in bytes.
 */
void NimBLECharacteristic::setValue(const uint8_t* data, size_t length) {
    char* pHex = NimBLEUtils::buildHexData(nullptr, data, length);
    NIMBLE_LOGD(LOG_TAG, ">> setValue: length=%d, data=%s, characteristic UUID=%s", length, pHex, getUUID().toString().c_str());
    free(pHex);

    if (length > BLE_ATT_ATTR_MAX_LEN) {
        NIMBLE_LOGE(LOG_TAG, "Size %d too large, must be no bigger than %d", length, BLE_ATT_ATTR_MAX_LEN);
        return;
    }

    m_value.setValue(data, length);

    NIMBLE_LOGD(LOG_TAG, "<< setValue");
} // setValue


/**
 * @brief Set the value of the characteristic from string data.
 * We set the value of the characteristic from the bytes contained in the
 * string.
 * @param [in] Set the value of the characteristic.
 * @return N/A.
 */
void NimBLECharacteristic::setValue(const std::string &value) {
    setValue((uint8_t*)(value.data()), value.length());
} // setValue

void NimBLECharacteristic::setValue(uint16_t& data16) {
    uint8_t temp[2];
    temp[0] = data16;
    temp[1] = data16 >> 8;
    setValue(temp, 2);
} // setValue

void NimBLECharacteristic::setValue(uint32_t& data32) {
    uint8_t temp[4];
    temp[0] = data32;
    temp[1] = data32 >> 8;
    temp[2] = data32 >> 16;
    temp[3] = data32 >> 24;
    setValue(temp, 4);
} // setValue

void NimBLECharacteristic::setValue(int& data32) {
    uint8_t temp[4];
    temp[0] = data32;
    temp[1] = data32 >> 8;
    temp[2] = data32 >> 16;
    temp[3] = data32 >> 24;
    setValue(temp, 4);
} // setValue

void NimBLECharacteristic::setValue(float& data32) {
    float temp = data32;
    setValue((uint8_t*)&temp, 4);
} // setValue

void NimBLECharacteristic::setValue(double& data64) {
    double temp = data64;
    setValue((uint8_t*)&temp, 8);
} // setValue


/**
 * @brief Return a string representation of the characteristic.
 * @return A string representation of the characteristic.
 */
std::string NimBLECharacteristic::toString() {
    std::string res = "UUID: " + m_uuid.toString() + ", handle : 0x";
    char hex[5];
    snprintf(hex, sizeof(hex), "%04x", m_handle);
    res += hex;
    res += " ";
    if (m_properties & BLE_GATT_CHR_PROP_READ ) res += "Read ";
    if (m_properties & BLE_GATT_CHR_PROP_WRITE) res += "Write ";
    if (m_properties & BLE_GATT_CHR_PROP_WRITE_NO_RSP) res += "WriteNoResponse ";
    if (m_properties & BLE_GATT_CHR_PROP_BROADCAST) res += "Broadcast ";
    if (m_properties & BLE_GATT_CHR_PROP_NOTIFY) res += "Notify ";
    if (m_properties & BLE_GATT_CHR_PROP_INDICATE) res += "Indicate ";
    return res;
} // toString


NimBLECharacteristicCallbacks::~NimBLECharacteristicCallbacks() {}


/**
 * @brief Callback function to support a read request.
 * @param [in] pCharacteristic The characteristic that is the source of the event.
 */
void NimBLECharacteristicCallbacks::onRead(NimBLECharacteristic* pCharacteristic) {
    NIMBLE_LOGD("NimBLECharacteristicCallbacks", "onRead: default");
} // onRead


/**
 * @brief Callback function to support a write request.
 * @param [in] pCharacteristic The characteristic that is the source of the event.
 */
void NimBLECharacteristicCallbacks::onWrite(NimBLECharacteristic* pCharacteristic) {
    NIMBLE_LOGD("NimBLECharacteristicCallbacks", "onWrite: default");
} // onWrite


/**
 * @brief Callback function to support a Notify request.
 * @param [in] pCharacteristic The characteristic that is the source of the event.
 */
void NimBLECharacteristicCallbacks::onNotify(NimBLECharacteristic* pCharacteristic) {
    NIMBLE_LOGD("NimBLECharacteristicCallbacks", "onNotify: default");
} // onNotify


/**
 * @brief Callback function to support a Notify/Indicate Status report.
 * @param [in] pCharacteristic The characteristic that is the source of the event.
 * @param [in] s Status of the notification/indication
 * @param [in] code Additional code of underlying errors
 */
void NimBLECharacteristicCallbacks::onStatus(NimBLECharacteristic* pCharacteristic, Status s, int code) {
    NIMBLE_LOGD("NimBLECharacteristicCallbacks", "onStatus: default");
} // onStatus

#endif // #if defined(CONFIG_BT_NIMBLE_ROLE_PERIPHERAL)
#endif /* CONFIG_BT_ENABLED */
