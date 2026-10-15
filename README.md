# AutoDoor

ESP32 firmware that opens a door with a servo. It opens on an MQTT message or from a built-in web page.

## How it works

- **MQTT** – connects over TLS and subscribes to one topic. A message equal to the open command (`openup` by default) opens the door. Retained messages are ignored.
- **Web UI** – HTTPS on port 443: open the door, set Wi-Fi, start an OTA update, show device info.
- **Wi-Fi** – always runs a config AP. Reconnects with backoff (1 s, doubling up to 300 s).
- **OTA** – downloads a `.bin` from an HTTPS URL, verified against `certs/ca.crt`. If the new firmware isn't healthy within a few seconds (door, Wi-Fi, MQTT and web server all up), the bootloader rolls back.
- **Servo** – A PWM drived servo driver. 

## Hardware

- ESP32 serial
- PWM servo

## Build

The `espressif/mqtt` component is fetched automatically.

1. Create `certs/` with:
    - `ca.crt` – the CA that signed your firmware server's certificate
    - `esp_server.ext`, `ota_server.ext` - extention file for certs
    - `esp_server.crt`, `esp_server.key` – certificate for the web UI
    - `ota_server.crt`, `ota_server.key` - certificate for OTA server

    .ext:
    ```
    [ server_cert ]
    basicConstraints = critical, CA:FALSE
    keyUsage = critical, digitalSignature, keyEncipherment
    extendedKeyUsage = serverAuth
    subjectAltName = @alt_names
    subjectKeyIdentifier = hash
    authorityKeyIdentifier = keyid,issuer

    [ alt_names ]
    DNS.1 = ...
    IP.1 = ...
    IP.2 = ...
    ```

    ```
    openssl genpkey -algorithm RSA -out ca.key -pkeyopt rsa_keygen_bits:4096
    openssl req -x509 -new -key ca.key -sha256 -days 3650 -out ca.crt \
        -subj "/C=CN/ST=Beijing/L=Beijing/O=MyOrg/OU=IT/CN=My Root CA" \
        -addext "basicConstraints=critical,CA:TRUE" \
        -addext "keyUsage=critical,keyCertSign,cRLSign" \
        -addext "subjectKeyIdentifier=hash"

    openssl ecparam -name prime256v1 -genkey -noout -out esp_server.key  # lower performance needed
    openssl req -new -key esp_server.key -out esp_server.csr -subj "/C=CN/ST=Beijing/L=Beijing/O=MyOrg/OU=IT/CN=My ESP Server"
    openssl x509 -req -in esp_server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out esp_server.crt -days 365 -sha256 -extfile esp_server.ext -extensions server_cert
    openssl verify -CAfile ca.crt esp_server.crt

    openssl genpkey -algorithm RSA -out ota_server.key -pkeyopt rsa_keygen_bits:2048
    openssl req -new -key ota_server.key -out ota_server.csr -subj "/C=CN/ST=Beijing/L=Beijing/O=MyOrg/OU=IT/CN=My OTA Server"
    openssl x509 -req -in ota_server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out ota_server.crt -days 365 -sha256 -extfile ota_server.ext -extensions server_cert
    openssl verify -CAfile ca.crt ota_server.crt
    ```

2. `idf.py menuconfig` → **AutoDoor: …** menus. Set at least the MQTT client ID and AP password.
3. `idf.py build flash monitor`

## First setup

1. Join the device's AP (default SSID `door`, password `password`).
2. Open `https://192.168.4.1` (accept the certificate warning).
3. Enter your router's SSID and password. The device reboots and connects.

## Notes

- The web UI has no login. Anyone who can reach the device (via the AP or your LAN) can open the door, change Wi-Fi settings or start an OTA.
- `certs/esp_server.key` is embedded in the firmware image. Don't publish the `.bin`.
