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
