import socket
import struct

# Predefined rooms
ROOMS = {
    "default": [],
    "room1": [],
    "room2": [],
    "discord.test": []
}

# Simple connection state tracking by client address
client_states = {}

# Constants for message types (UDP)
UDP_TYPE_CONFIRM = 0x00
UDP_TYPE_REPLY   = 0x01
UDP_TYPE_AUTH    = 0x02
UDP_TYPE_JOIN    = 0x03
UDP_TYPE_MSG     = 0x04
UDP_TYPE_PING    = 0xFD
UDP_TYPE_ERR     = 0xFE
UDP_TYPE_BYE     = 0xFF

def build_datagram(msg_type, msg_id, payload_bytes=b""):
    header = struct.pack("!BH", msg_type, msg_id)
    return header + payload_bytes

def parse_zero_terminated_strings(payload):
    parts = []
    current = []
    for b in payload:
        if b == 0:
            parts.append(bytes(current).decode('ascii', errors='replace'))
            current = []
        else:
            current.append(b)
    if current:
        parts.append(bytes(current).decode('ascii', errors='replace'))
    return parts

def print_room_states():
    print("--- Room states ---")
    for room, messages in ROOMS.items():
        print(f"{room}:")
        if messages:
            for msg in messages:
                print(f"  {msg}")
        else:
            print("  (empty)")
    print("-------------------")

def broadcast_to_room(sock, sender, room, message, exclude_addr=None):
    formatted = f"{sender}: {message}"
    ROOMS[room].append(formatted)
    print_room_states()

def handle_auth(state, parts, sock, addr):
    if len(parts) < 3:
        return (False, "Malformed AUTH data")
    username = parts[0]
    display_name = parts[1]
    secret = parts[2]
    state["authenticated"] = True
    state["displayName"] = display_name
    state["currentRoom"] = "default"
    
    print(f"AUTH received: username={username}, display_name={display_name}, secret={secret}")

    print(f"{display_name} has authenticated and joined default.")
    broadcast_to_room(sock, "Server", "default", f"{display_name} has joined the room.")
    return (True, "Auth success.")

def handle_join(state, parts, sock, addr):
    if not state.get("authenticated"):
        return (False, "You must AUTH first.")
    if len(parts) < 2:
        return (False, "Malformed JOIN data")
    channel = parts[0]
    disp = parts[1]
    if disp != state["displayName"]:
        return (False, "Wrong display name.")
    if channel not in ROOMS:
        return (False, "Unknown channel.")
    old_ch = state["currentRoom"]
    if old_ch:
        state_msgs = ROOMS[old_ch]
        state_msgs.append(f"Server: {disp} has left the room.")
    state["currentRoom"] = channel

    print(f"{disp} moved from {old_ch} to {channel}")
    broadcast_to_room(sock, "Server", channel, f"{disp} has joined the room.")
    return (True, f"Join success. {disp} joined {channel}.")

def handle_msg(state, parts):
    if not state.get("authenticated"):
        return (False, "You must AUTH first.")
    if len(parts) < 2:
        return (False, "Malformed MSG data")
    from_disp = parts[0]
    message = parts[1]
    if from_disp != state["displayName"]:
        return (False, "DisplayName mismatch.")
    return (True, message)

def main():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(('127.0.0.1', 4567))
    print("UDP Server running on 127.0.0.1:4567")

    msg_id_counter = 1000

    while True:
        data, addr = s.recvfrom(65535)
        if len(data) < 3:
            print(f"Received too short datagram from {addr}")
            continue

        msg_type = data[0]
        msg_id = (data[1] << 8) | data[2]
        payload = data[3:]
        print(f"Received {msg_type} from {addr}, msg_id={msg_id}, payload={payload}")

        if addr not in client_states:
            client_states[addr] = {
                "authenticated": False,
                "displayName": None,
                "currentRoom": None
            }
        state = client_states[addr]

        if msg_type == UDP_TYPE_CONFIRM:
            print(f"Client {addr} confirmed messageID={msg_id}")
            continue
        if msg_type == UDP_TYPE_PING:
            print(f"Received PING from {addr}, sending confirm")
            confirm_bytes = build_datagram(UDP_TYPE_CONFIRM, msg_id)
            s.sendto(confirm_bytes, addr)
            continue

        confirm_bytes = build_datagram(UDP_TYPE_CONFIRM, msg_id)
        s.sendto(confirm_bytes, addr)

        if msg_type == UDP_TYPE_AUTH:
            parts = parse_zero_terminated_strings(payload)
            ok, text = handle_auth(state, parts, s, addr)
            server_msg_id = msg_id_counter
            msg_id_counter += 1

            result_byte = b'\x01' if ok else b'\x00'
            ref_msg_id_be = struct.pack("!H", msg_id)
            msg_content = text.encode('ascii', errors='replace') + b'\x00'
            payload_reply = result_byte + ref_msg_id_be + msg_content
            reply_dgram = build_datagram(UDP_TYPE_REPLY, server_msg_id, payload_reply)
            s.sendto(reply_dgram, addr)

        elif msg_type == UDP_TYPE_JOIN:
            parts = parse_zero_terminated_strings(payload)
            ok, text = handle_join(state, parts, s, addr)
            server_msg_id = msg_id_counter
            msg_id_counter += 1

            result_byte = b'\x01' if ok else b'\x00'
            ref_msg_id_be = struct.pack("!H", msg_id)
            msg_content = text.encode('ascii', errors='replace') + b'\x00'
            payload_reply = result_byte + ref_msg_id_be + msg_content
            reply_dgram = build_datagram(UDP_TYPE_REPLY, server_msg_id, payload_reply)
            s.sendto(reply_dgram, addr)

        elif msg_type == UDP_TYPE_MSG:
            parts = parse_zero_terminated_strings(payload)
            ok, message = handle_msg(state, parts)
            print(f"is it ok: {ok}, message: {message}")
            if ok:
                display_name = state["displayName"] or "???"
                room = state["currentRoom"]
                broadcast_to_room(s, display_name, room, message)
            else:
                print(f"Error in message from {addr}: {message}")
                server_msg_id = msg_id_counter
                msg_id_counter += 1
                disp = state["displayName"] if state["displayName"] else "???"
                content = message
                err_payload = (disp + "\0" + content + "\0").encode('ascii', errors='replace')
                err_dgram = build_datagram(UDP_TYPE_ERR, server_msg_id, err_payload)
                s.sendto(err_dgram, addr)

        elif msg_type == UDP_TYPE_BYE:
            disp = state["displayName"] if state["displayName"] else "???"
            room = state["currentRoom"]
            if room:
                broadcast_to_room(s, "Server", room, f"{disp} has left the room.")
            print(f"{disp} has disconnected.")
            del client_states[addr]

        else:
            print(f"Unknown msg_type={msg_type} from {addr}")
            server_msg_id = msg_id_counter
            msg_id_counter += 1
            result_byte = b'\x00'
            ref_msg_id_be = struct.pack("!H", msg_id)
            msg_content = b"Unknown command\x00"
            payload_reply = result_byte + ref_msg_id_be + msg_content
            reply_dgram = build_datagram(UDP_TYPE_REPLY, server_msg_id, payload_reply)
            s.sendto(reply_dgram, addr)

if __name__ == "__main__":
    main()
