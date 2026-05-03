import asyncio
from bleak import BleakClient, BleakScanner

CHAR_UUID = "abcd1234-ab12-cd34-ef56-abcdef123456"

def handle_notify(sender, data):
    print("ESP32 >>", data.decode("utf-8"))

async def main():
    print("Scanning for Float-Test...")
    device = await BleakScanner.find_device_by_name("Float-Test")
    if not device:
        print("Device not found! Is the ESP32 powered on?")
        return

    async with BleakClient(device) as client:
        await client.start_notify(CHAR_UUID, handle_notify)
        print("Connected! Type a command and press Enter.\n")

        while True:
            cmd = input(">> ")
            if cmd.strip() == "":
                continue
            await client.write_gatt_char(CHAR_UUID, cmd.encode())

asyncio.run(main())
