#!/usr/bin/env python3

# Script to download/update certificates and public keys
# and generate compilable source files for c++/Arduino.
# released to public domain

import urllib.request
import re
import ssl
import sys
import socket
import argparse
import datetime

from cryptography import x509
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.serialization import pkcs7
from cryptography.hazmat.primitives.serialization import Encoding
from cryptography.hazmat.primitives.serialization import PublicFormat

def printData(data, showPub = True):
    try:
        xcert = x509.load_der_x509_certificate(data)
    except:
        try:
            xcert = x509.load_pem_x509_certificate(data)
        except:
            try:
                xcert = pkcs7.load_der_pkcs7_certificates(data)
            except:
                xcert = pkcs7.load_pem_pkcs7_certificates(data)
            if len(xcert) > 1:
                print('// Warning: TODO: pkcs7 has {} entries'.format(len(xcert)))
            xcert = xcert[0]

    cn = ''
    for dn in xcert.subject.rfc4514_string().split(','):
        keyval = dn.split('=')
        if keyval[0] == 'CN':
            cn += keyval[1]
    name = re.sub('[^a-zA-Z0-9_]', '_', cn)
    print('// CN: {} => name: {}'.format(cn, name))

    print('// not valid before:', xcert.not_valid_before_utc)
    print('// not valid after: ', xcert.not_valid_after_utc)

    if showPub:

        fingerprint = xcert.fingerprint(hashes.SHA1()).hex(':')
        print('const char fingerprint_{} [] PROGMEM = "{}";'.format(name, fingerprint))

        pem = xcert.public_key().public_bytes(Encoding.PEM, PublicFormat.SubjectPublicKeyInfo).decode('utf-8')
        print('const char pubkey_{} [] PROGMEM = R"PUBKEY('.format(name))
        print(pem + ')PUBKEY";')
    
    else:

        cert = xcert.public_bytes(Encoding.PEM).decode('utf-8')
        print('const char cert_{} [] PROGMEM = R"CERT('.format(name))
        print(cert + ')CERT";')

    cas = []
    for ext in xcert.extensions:
        if ext.oid == x509.ObjectIdentifier("1.3.6.1.5.5.7.1.1"):
            for desc in ext.value:
                if desc.access_method == x509.oid.AuthorityInformationAccessOID.CA_ISSUERS:
                    cas.append(desc.access_location.value)
    for ca in cas:
        with urllib.request.urlopen(ca) as crt:
            print()
            print('// ' + ca)
            printData(crt.read(), False)
        print()

def get_certificate(hostname, port, name):
    context = ssl.create_default_context()
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    with socket.create_connection((hostname, port)) as sock:
        with context.wrap_socket(sock, server_hostname=hostname) as ssock:
            print('////////////////////////////////////////////////////////////')
            print('// certificate chain for {}:{}'.format(hostname, port))
            print()
            if name:
                print('const char* {}_host = "{}";'.format(name, hostname));
                print('const uint16_t {}_port = {};'.format(name, port));
                print()
            printData(ssock.getpeercert(binary_form=True))
            print('// end of certificate chain for {}:{}'.format(hostname, port))
            print('////////////////////////////////////////////////////////////')
            print()
            return 0

def main():
    parser = argparse.ArgumentParser(description='download certificate chain and public keys under a C++/Arduino compilable form')
    parser.add_argument('-s', '--server', action='store', required=True, help='TLS server dns name')
    parser.add_argument('-p', '--port', action='store', required=False, help='TLS server port')
    parser.add_argument('-n', '--name', action='store', required=False, help='variable name')
    port = 443
    args = parser.parse_args()
    server = args.server
    port = 443
    try:
        split = server.split(':')
        server = split[0]
        port = int(split[1])
    except:
        pass
    try:
        port = int(args.port)
    except:
        pass

    print()
    print('// this file is autogenerated - any modification will be overwritten')
    print('// unused symbols will not be linked in the final binary')
    print('// generated on {}'.format(datetime.datetime.now(datetime.UTC).strftime("%Y-%m-%d %H:%M:%S")))
    print('// by {}'.format(sys.argv))
    print()
    print('#pragma once')
    print()
    return get_certificate(server, port, args.name)

if __name__ == '__main__':
    sys.exit(main())
