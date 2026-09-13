from random import randint
import serial, sys, glob
from pathlib import Path
from zlib import crc32
import PySimpleGUI as sg
import serial.tools.list_ports

"""
Intro:
    This program is here to communicate with the Nucleo from ur computer.
    It uses a simple custom protocol to send data and verify its validity.
    This is because UART can be unreliable some times. (It's not UART anymore
    now we use CRC, but it's really close)
    Have fun ;)
    - Ayabusa

Usage:
    python3 file_transfer.py <download/upload> <device, ex: /dev/ttyACM67> <file_path>

Packet structure:
    |============Header===========|===packet===|=Footer==|
    +---------+---------+---------+------------+---------+
    | MagicNB | Type    | ID      | Extra data | CRC32   |
    | 4 bytes | 2 byte  | 2 byte  | ?? bytes   | 4 bytes |
    +---------+---------+---------+------------+---------+
    MagicNB = 1337_4267h, this is to say that the packet starts
    Type = This say what the packet will do:
        - 0 : Acknoledge the packet with ID
        - 1 : Erase chip
        - 2 : Send file to ROM
        - 3 : Receive file from ROM
        - A : Data packet (This can come from the PC or the ROM)
    ID = Unique packet ID, it can be used to acknoledge another packet
    Extra data = This is the core data such as binary data for example
    CRC32 = Checksum that verify the packet integrity

Extra data:
    Acknoledge packet:
        +---------+---------+---------+
        | Header  | Status  | Footer  |
        | 8 bytes | 2 bytes | 4 bytes |
        +---------+---------+---------+
        Status :
            - 0 : All good buddy
            - 1 : Invalid checksum please resend

    Data packet:
        +---------+---------+-----------+---------+
        | Header  | Address | Data      | Footer  |
        | 8 bytes | 4 bytes | 128 bytes | 4 bytes |
        +---------+---------+-----------+---------+

Note:
    The CRC32 is computed from the Address and the Data section combined,
    A response is given by the PC or NUCLEO to assess the validity of the checksum.
"""
BAUDRATE = 9600
PACKET_NUMBER = 8192
PACKET_ADDRESS_SIZE = 4
PACKET_DATA_SIZE = 128
PACKET_CRC32_SIZE = 4
PACKET_SIZE = PACKET_ADDRESS_SIZE + PACKET_DATA_SIZE + PACKET_CRC32_SIZE
BYTEORDER_ADDRESS = 'little'
BYTEORDER_CRC32 = 'little'

TYPE_ACK     = 0
TYPE_ERASE   = 1
TYPE_SEND    = 2
TYPE_RECEIVE = 3
TYPE_TEST    = 4
TYPE_DATA    = 0xA

STATUS_GOOD = 0
STATUS_BAD  = 1

SIZE_MAGIC_NB = 4
SIZE_TYPE = 2
SIZE_ID = 2
SIZE_HEADER = SIZE_MAGIC_NB + SIZE_TYPE + SIZE_ID
SIZE_FOOTER = 4
SIZE_ED_STATUS = 2
SIZE_ED_DATA = PACKET_DATA_SIZE + PACKET_ADDRESS_SIZE

MAGIC_NB = 0x13374267

ICON = b"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"

ser = None

"""
def create_packet(file: bytearray, address: int):
    packet = bytearray()
    packet += (address//2).to_bytes(4, byteorder=BYTEORDER_ADDRESS)
    packet += file[address:address+PACKET_DATA_SIZE]
    checksum = crc32(packet)
    packet += checksum.to_bytes(4, byteorder=BYTEORDER_CRC32)
    return packet
"""

def send_packet(packet: bytearray)-> bool:
    print("Sending packet:", ''.join('{:02x} '.format(x) for x in packet))
    status = STATUS_BAD
    while status is not STATUS_GOOD:
        ser.write(packet)
        ack = bytearray(ser.read(SIZE_HEADER + SIZE_ED_STATUS + SIZE_FOOTER))
        if (len(ack) != SIZE_HEADER + SIZE_ED_STATUS + SIZE_FOOTER):
            print("Bad ACK:", ''.join('{:02x} '.format(x) for x in ack))
            return False
        status = int.from_bytes(ack[SIZE_HEADER:SIZE_HEADER+SIZE_ED_STATUS])
    return True

def receive_packet(size: int)-> (int, bytearray): # returns (type, extra_data)
    while True:
        packet = bytearray(ser.read(size))
        if (len(packet) < size):
            return (None, None)
        received_checksum = int.from_bytes(packet[size-SIZE_FOOTER:], byteorder='little')
        computed_checksum = crc32(packet[:size-SIZE_FOOTER])
        id = int.from_bytes(packet[SIZE_MAGIC_NB + SIZE_TYPE:SIZE_ID])
        # We send a response depending on the crc32 validity
        if received_checksum == computed_checksum:
            ser.write(create_ack_packet(id, STATUS_GOOD))
            type = int.from_bytes(packet[SIZE_MAGIC_NB:SIZE_TYPE], byteorder='little')
            extra_data = packet[SIZE_HEADER:size - SIZE_FOOTER]
            return (type, extra_data)
        # We loop if crc is bad
        ser.write(create_ack_packet(id, STATUS_BAD))

def receive_data_packet()-> (int, bytearray): # returns (address, data)
    type, extra_data = receive_packet(SIZE_HEADER + SIZE_ED_DATA + SIZE_FOOTER)
    if type == None:
        return (None, None)
    address = int.from_bytes(extra_data[:PACKET_ADDRESS_SIZE], byteorder='little')
    data = extra_data[PACKET_ADDRESS_SIZE:]
    return (address, data)

def send_file(file: bytes):
    for i in range(PACKET_NUMBER):
        good = b'B'
        while good != b'G':
            print(f"Writing packet {i+1}/{PACKET_NUMBER}")
            packet = create_packet(bytearray(file), i * PACKET_DATA_SIZE)
            send_packet(packet)
            good = ser.read(1)

def get_file(file):
    file_buffer = bytearray(0x80000)
    i = 0
    while i < PACKET_NUMBER:
        packet = bytearray(ser.read(PACKET_SIZE))
        received_checksum = int.from_bytes(packet[PACKET_ADDRESS_SIZE + PACKET_DATA_SIZE:], byteorder='little')
        computed_checksum = crc32(packet[:PACKET_ADDRESS_SIZE + PACKET_DATA_SIZE])
        if received_checksum != computed_checksum:
            print("Got a bad checksum, rejecting.")
            ser.write(b'B')
            continue
        address = int.from_bytes(packet[:PACKET_ADDRESS_SIZE], byteorder='little')
        data = packet[PACKET_ADDRESS_SIZE:PACKET_ADDRESS_SIZE + PACKET_DATA_SIZE]
        file_buffer[address*2:address*2+128] = data
        ser.write(b'G')
        i+=1
        print(f"Received packet {i}/{PACKET_NUMBER}")

    file.write(bytes(file_buffer))

def serial_ports():
    """ Lists serial port names

        :raises EnvironmentError:
            On unsupported or unknown platforms
        :returns:
            A list of the serial ports available on the system
    """
    if sys.platform.startswith('win'):
        ports = ['COM%s' % (i + 1) for i in range(256)]
    elif sys.platform.startswith('linux') or sys.platform.startswith('cygwin'):
        # this excludes your current terminal "/dev/tty"
        ports = glob.glob('/dev/tty[A-Za-z]*')
    elif sys.platform.startswith('darwin'):
        ports = glob.glob('/dev/tty.*')
    else:
        raise EnvironmentError('Unsupported platform')

    result = []
    for port in ports:
        try:
            s = serial.Serial(port)
            s.close()
            result.append(port)
        except (OSError, serial.SerialException):
            pass
    return result

def create_packet(type: int, extra_data=None, id=None) -> bytearray:
    packet = bytearray()
    packet += MAGIC_NB.to_bytes(4, byteorder=BYTEORDER_ADDRESS)
    packet += type.to_bytes(2, byteorder=BYTEORDER_ADDRESS)
    if (id is None):
        packet += randint(0,255).to_bytes(2, byteorder=BYTEORDER_ADDRESS) # id
    else:
        packet += id.to_bytes(2, byteorder=BYTEORDER_ADDRESS) # id
    if (extra_data is not None):
        packet += extra_data
    checksum = crc32(packet)
    packet += checksum.to_bytes(4, byteorder=BYTEORDER_CRC32)
    return packet

def create_data_packet(address: int, file: bytearray) -> bytearray:
    data = bytearray()
    data += (address//2).to_bytes(4, byteorder=BYTEORDER_ADDRESS)
    data += file[address:address+PACKET_DATA_SIZE]
    return create_packet(TYPE_DATA, data)

def create_ack_packet(id: int, status: int) -> bytearray:
    data = bytearray()
    data += status.to_bytes(2, byteorder=BYTEORDER_ADDRESS)
    return create_packet(TYPE_ACK, data, id)

sg.theme('LightBrown4')
# Window layout
layout = [[sg.Text("TI-92 Brainwasher utility", font=('Calibri', 20))],
            [sg.Text("Serial port"), sg.Combo([], key='-PORTS-', enable_events=True), sg.Button('Refresh')],
            [sg.Button('Test connection')],
            [sg.Button('Erase ROM')],
            [sg.Button('Dump ROM')],
            [sg.Button('Write ROM')],
            [sg.Text(size=(40,1), key='-OUTPUT-')],
            [sg.ProgressBar(PACKET_NUMBER, key='-PROGRESS_BAR-', size=(10,20), expand_x=True)]]


sg.set_global_icon(ICON)
# Create the window
window = sg.Window('TI-92 Brainwasher', layout)

# Display and interact with the Window using an Event Loop
count = 0
while True:
    event, values = window.read()
    # See if user wants to quit or window was closed
    if event == sg.WINDOW_CLOSED or event == 'Quit':
        if ser:
            ser.close()
        break
    # Output a message to the window
    if (event == 'Refresh'):
        window['-PORTS-'].update(values=serial_ports())

    elif (event == '-PORTS-'):
        if ser is not None:
            ser.close()
        ser = serial.Serial(window['-PORTS-'].get(), timeout=2) # opening da serial port
        window['-OUTPUT-'].update("Opened serial port : " + ser.name)

    elif ser is None:
        sg.popup("Please select a serial port first")
        continue

    elif (event == 'Test connection'):
        if not send_packet(create_packet(TYPE_TEST)):
            sg.popup("The Brainwasher did not respond")
        else:
            sg.popup("The Brainwasher is responding !")

    elif (event == 'Erase ROM'):
        if not send_packet(create_packet(TYPE_ERASE)):
            sg.popup("The Brainwasher did not respond")
        sg.popup("The ROM has been erased")
        window['-OUTPUT-'].update("The rom has been erased")

    elif (event == 'Write ROM'):
        filepath = sg.popup_get_file("Please select the file you wish to send")
        if filepath:
            fileb = Path(filepath).read_bytes()
            if not send_packet(create_packet(TYPE_SEND)):
                sg.popup("The Brainwasher did not respond")
            else:
                window['-OUTPUT-'].update("Writing the ROM")
                good = True
                for i in range(PACKET_NUMBER):
                    print(f"Writing packet {i+1}/{PACKET_NUMBER}")
                    # We send each packet
                    if not send_packet(create_data_packet(i * PACKET_DATA_SIZE, fileb)):
                        sg.popup("The Brainwasher did not respond during a data transfer, aborting")
                        good = False
                        break
                    window['-PROGRESS_BAR-'].update(current_count=i) # Update progress bar
                if good:
                    sg.popup("The ROM has been written, I suggest you read it back to verify it's integrity")
                    window['-OUTPUT-'].update("The ROM has been written")

    elif (event == 'Dump ROM'):
        filepath = sg.popup_get_file("Please select where you wish to save the dump", save_as=True)
        if filepath:
            if not send_packet(create_packet(TYPE_RECEIVE)):
                sg.popup("The Brainwasher did not respond")

            window['-OUTPUT-'].update("Dumping the ROM")
            file = open(Path(filepath), "wb") # We open the file to write to
            file_buffer = bytearray(0x80000)
            i = 0
            while i < PACKET_NUMBER:
                address, data = receive_data_packet()
                file_buffer[address*2:address*2+128] = data
                i+=1
                print(f"Received packet {i}/{PACKET_NUMBER}")
                window['-PROGRESS_BAR-'].update(current_count=i) # Update progress bar

            file.write(bytes(file_buffer))
            file.close()
            sg.popup("The ROM has been read")
            window['-OUTPUT-'].update("The ROM has been dumped")

    else:
        print(event)

# Finish up by removing from the screen
window.close()

"""
if len(sys.argv) != 4 or (sys.argv[1] != 'download' and sys.argv[1] != 'upload'):
    print("Usage: python3 file_transfer.py <download/upload> <device, ex: /dev/ttyACM67> <file_path>")
    sys.exit()

ser = serial.Serial(sys.argv[2], BAUDRATE) # opening da serial port
print("Serial name:", ser.name)         # check which port was really used

if sys.argv[1] == 'upload':
    file = Path(sys.argv[3]).read_bytes()
    send_file(file)
else:
    file = open(Path(sys.argv[3]), "wb")
    get_file(file)
    file.close()

print("Done, cya.")
ser.close()
"""
