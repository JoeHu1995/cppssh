/*
    cppssh - C++ ssh library
    Copyright (C) 2015  Chris Desjardins
    http://blog.chrisd.info cjd@chrisd.info

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "kex.h"
#include "messages.h"
#include "impl.h"
#include "packet.h"

CppsshKex::CppsshKex(const std::shared_ptr<CppsshSession> &session)
    : _session(session)
{

}

void CppsshKex::constructLocalKex()
{
    std::vector<Botan::byte> random;
    std::string kex;
    std::string hostkey;
    std::string compressors;
    std::string ciphersStr;
    std::string hmacsStr;
    
    _localKex.clear();
    _localKex.push_back(SSH2_MSG_KEXINIT);

    random.resize(16);
    CppsshImpl::RNG->randomize(random.data(), random.size());

    std::copy(random.begin(), random.end(), std::back_inserter(_localKex));
    CppsshImpl::vecToCommaString(CppsshImpl::KEX_ALGORITHMS, std::string(), &kex, NULL);
    CppsshImpl::vecToCommaString(CppsshImpl::HOSTKEY_ALGORITHMS, std::string(), &hostkey, NULL);

    CppsshImpl::vecToCommaString(CppsshImpl::CIPHER_ALGORITHMS, CppsshImpl::PREFERED_CIPHER, &ciphersStr, &_ciphers);
    CppsshImpl::vecToCommaString(CppsshImpl::MAC_ALGORITHMS, CppsshImpl::PREFERED_MAC, &hmacsStr, &_hmacs);
    CppsshImpl::vecToCommaString(CppsshImpl::COMPRESSION_ALGORITHMS, std::string(), &compressors, NULL);

    CppsshPacket localKex(&_localKex);

    Botan::secure_vector<Botan::byte> ciphers(ciphersStr.begin(), ciphersStr.end());
    Botan::secure_vector<Botan::byte> hmacs(hmacsStr.begin(), hmacsStr.end());

    localKex.addVectorField(ciphers);
    localKex.addVectorField(ciphers);
    localKex.addVectorField(hmacs);
    localKex.addVectorField(hmacs);
    localKex.addString(compressors);
    localKex.addString(compressors);
    localKex.addInt(0);
    localKex.addInt(0);
    localKex.addChar('\0');
    localKex.addInt(0);
}

bool CppsshKex::sendInit()
{
    bool ret = true;

    constructLocalKex();
    
    if (_session->_transport->sendPacket(_localKex) == false)
    {
        ret = false;
    }
    else if (_session->_transport->waitForPacket(SSH2_MSG_KEXINIT) == 0)
    {
        //ne7ssh::errors()->push(_session->getSshChannel(), "Timeout while waiting for key exchange init reply");
        ret = false;
    }

    return ret;
}

bool CppsshKex::handleInit()
{
    Botan::secure_vector<Botan::byte> packet;
    uint32_t padLen = _session->_transport->getPacket(packet);
    Botan::secure_vector<Botan::byte> remoteKexAlgos(packet.begin() + 17, packet.end() - 17);
    Botan::secure_vector<Botan::byte> algos;
    Botan::secure_vector<Botan::byte> agreed;

    if ((_session->_transport == NULL) || (_session->_crypto == NULL))
    {
        return false;
    }
    _remoteKex.clear();
    CppsshPacket remoteKexPacket(&_remoteKex);
    remoteKexPacket.addVector(Botan::secure_vector<Botan::byte>(packet.begin(), (packet.begin() + (packet.size() - padLen - 1))));
    CppsshPacket remoteKexAlgosPacket(&remoteKexAlgos);

    if (remoteKexAlgosPacket.getString(algos) == false)
    {
        return false;
    }
    if (_session->_crypto->agree(agreed, CppsshImpl::KEX_ALGORITHMS, algos) == false)
    {
        //ne7ssh::errors()->push(_session->getSshChannel(), "No compatible key exchange algorithms.");
        return false;
    }
    if (_session->_crypto->negotiatedKex(agreed) == false)
    {
        return false;
    }
    if (remoteKexAlgosPacket.getString(algos) == false)
    {
        return false;
    }
    if (_session->_crypto->agree(agreed, CppsshImpl::HOSTKEY_ALGORITHMS, algos) == false)
    {
        //ne7ssh::errors()->push(_session->getSshChannel(), "No compatible Hostkey algorithms.");
        return false;
    }
    if (_session->_crypto->negotiatedHostkey(agreed) == false)
    {
        return false;
    }
    if (remoteKexAlgosPacket.getString(algos) == false)
    {
        return false;
    }
    if (_session->_crypto->agree(agreed, _ciphers, algos) == false)
    {
        //ne7ssh::errors()->push(_session->getSshChannel(), "No compatible cryptographic algorithms.");
        return false;
    }
    if (!_session->_crypto->negotiatedCryptoC2s(agreed))
    {
        return false;
    }

    if (remoteKexAlgosPacket.getString(algos) == false)
    {
        return false;
    }
    if (_session->_crypto->agree(agreed, _ciphers, algos) == false)
    {
        //ne7ssh::errors()->push(_session->getSshChannel(), "No compatible cryptographic algorithms.");
        return false;
    }
    if (_session->_crypto->negotiatedCryptoS2c(agreed) == false)
    {
        return false;
    }
    if (remoteKexAlgosPacket.getString(algos) == false)
    {
        return false;
    }
    if (_session->_crypto->agree(agreed, _hmacs, algos) == false)
    {
        //ne7ssh::errors()->push(_session->getSshChannel(), "No compatible HMAC algorithms.");
        return false;
    }

    if (_session->_crypto->negotiatedMacC2s(agreed) == false)
    {
        return false;
    }

    if (remoteKexAlgosPacket.getString(algos) == false)
    {
        return false;
    }
    if (_session->_crypto->agree(agreed, _hmacs, algos) == false)
    {
        //ne7ssh::errors()->push(_session->getSshChannel(), "No compatible HMAC algorithms.");
        return false;
    }
    if (_session->_crypto->negotiatedMacS2c(agreed) == false)
    {
        return false;
    }

    if (remoteKexAlgosPacket.getString(algos) == false)
    {
        return false;
    }
    if (_session->_crypto->agree(agreed, CppsshImpl::COMPRESSION_ALGORITHMS, algos) == false)
    {
        //ne7ssh::errors()->push(_session->getSshChannel(), "No compatible compression algorithms.");
        return false;
    }

    if (_session->_crypto->negotiatedCmprsC2s(agreed) == false)
    {
        return false;
    }

    if (remoteKexAlgosPacket.getString(algos) == false)
    {
        return false;
    }
    if (_session->_crypto->agree(agreed, CppsshImpl::COMPRESSION_ALGORITHMS, algos) == false)
    {
        //ne7ssh::errors()->push(_session->getSshChannel(), "No compatible compression algorithms.");
        return false;
    }
    if (_session->_crypto->negotiatedCmprsS2c(agreed) == false)
    {
        return false; 
    }
    return true;
}

bool CppsshKex::sendKexDHInit()
{
    bool ret = true;
    Botan::BigInt publicKey;

    if (_session->_crypto->getKexPublic(publicKey) == false)
    {
        ret = false;
    }
    else
    {
        Botan::secure_vector<Botan::byte> buf;
        CppsshPacket dhInit(&buf);
        dhInit.addChar(SSH2_MSG_KEXDH_INIT);
        dhInit.addBigInt(publicKey);
        
        CppsshPacket::bn2vector(_e, publicKey);

        if (_session->_transport->sendPacket(buf) == false)
        {
            ret = false;
        }
        else if (_session->_transport->waitForPacket(SSH2_MSG_KEXDH_REPLY) <= 0)
        {
            //ne7ssh::errors()->push(_session->getSshChannel(), "Timeout while waiting for key exchange dh reply.");
            ret = false;
        }
    }
    return ret;
}
