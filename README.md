This is a lux-meter with Matter support. This means you can use it for getting the current light level inte your home automation system, I use Google Home. The hardware I use is a VEML7700 sensor and a ESP32C6, but you can just as well use a S3, C3 or whatever. The Esp32 do not have defined SCL and SCA pins, so you can use which ones you like, just adjust the code in veml7700_handler.cpp:

#define SDA_GPIO_NUM 5
#define SCL_GPIO_NUM 6

I use ESP-IDF v 5.4.1 and Matter 1.4.
Download Matter, https://github.com/espressif/esp-matter
and ESP-IDF, https://github.com/espressif/esp-idf
In VS Code, install the ESP-IDF extension and set it up to point to where you downloaded the SDK:s above.

Onboarding data
In a common Bash-terminal, generate onboarding data for getting the device into Google Home. The tool esp-matter-mfg-tool runs best in a virtual environment:

sudo apt install python3-full python3-venv
python3 -m venv ~/esp-matter-venv
source ~/esp-matter-venv/bin/activate
pip install --upgrade pip
pip install esp-matter-mfg-tool

Here you have to adjust the paths to your 
esp-matter-mfg-tool -cn "My sensor" -v 0xFFF2 -p 0x8001 --pai --discriminator 3845   --passcode 20202026 -k esp-matter/connectedhomeip/connectedhomeip/credentials/test/attestation/Chip-Test-PAI-FFF2-8001-Key.pem -c esp-matter/connectedhomeip/connectedhomeip/credentials/test/attestation/Chip-Test-PAI-FFF2-8001-Cert.pem -cd nvm/esp-matter/connectedhomeip/connectedhomeip/credentials/test/certification-declaration/Chip-Test-CD-FFF2-8001.der

If you make more than one device, the discriminator value must be unique. So for a second device, replace:
--discriminator 3841
with:
--discriminator 3842
in the command above.

Then run Docker to get the correct ESP-IDF and Matter versions:

docker run -it --rm --device=/dev/ttyACM0:/dev/ttyACM0 --privileged -v $PWD:/project espressif/esp-matter:latest /bin/bash

Then:

idf.py menuconfig

Enable ESP32 Factory Data Provider [Component config → CHIP Device Layer → Commissioning options → Use ESP32 Factory Data Provider]

Enable ESP32 Device Instance Info Provider [Component config → CHIP Device Layer → Commissioning options → Use ESP32 Device Instance Info Provider]

Enable Attestation - Factory [ Component config → ESP Matter → DAC Provider options → Attestation - Factory]

Set chip-factory namespace partition label [Component config → CHIP Device Layer → Matter Manufacturing Options → chip-factory namespace partition label] to fctry

Component config → CHIP Device Layer → Device Identification Options
Vendor Id = 0xFFF2
(0x8001) Device Product Id

Set the correct device:
idf.py set-target esp32c6

Erase flash,, compile and flash the new firmwarwe:
idf.py erase-flash -p /dev/ttyACM0
idf.py flash -p /dev/ttyACM0

Flash the onboarding data we created above:
esptool.py -p /dev/ttyACM0 write_flash 0x1E000 out/fff2_8001/39ca1f19-710e-465f-86b5-2cdf10c510ed/39ca1f19-710e-465f-86b5-2cdf10c510ed-partition.bin 

Start a serial monitor to see what happends:
idf.py monitor -p /dev/ttyACM0

And find the png-file in Out-folder and open it. Use the Google Home app to add the device by scanning the QR-code in the png.