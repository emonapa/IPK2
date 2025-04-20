import socket

ROOMS = {
    "default": set(),
    "room1": set(),
    "room2": set()
}

def parse_tcp_line(line: str):
    line = line.strip('\r\n')
    if line.startswith("AUTH "):
    # We expect the format: AUTH username AS displayName USING secret
    # We'll just split it up with a rough parse, extremely simplified here:
    # (We don't handle cases where there are no parameters, etc.)
        try:
            parts = line.split()
            username = parts[1]
            displayname = parts[3]
            secret = parts[5]
            return ("AUTH", {
                "username": username,
                "displayname": displayname,
                "secret": secret
            })
        except:
            return ("UNKNOWN", {})
    elif line.startswith("JOIN "):
        # JOIN channelID AS displayName
        try:
            parts = line.split()
            channel = parts[1]
            displayname = parts[3]
            return ("JOIN", {
                "channel": channel,
                "displayname": displayname
            })
        except:
            return ("UNKNOWN", {})
    elif line.startswith("MSG FROM "):
        # MSG FROM displayName IS messageContent
        try:
            after_from = line[len("MSG FROM "):]

            if " IS " not in after_from:
                return ("UNKNOWN", {})
            before_is, msg = after_from.split(" IS ", 1)
            displayname = before_is.strip()
            message = msg.strip()
            return ("MSG", {
                "displayname": displayname,
                "message": message
            })
        except:
            return ("UNKNOWN", {})
    else:
        return ("UNKNOWN", {})

def main():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(('127.0.0.1', 4567))
    s.listen(1)
    print("Server running on port 4567")
    conn, addr = s.accept()
    print("Client connected:", addr)
    
    authenticated = False
    current_channel = None
    user_display_name = None
    
    while True:
        data = conn.recv(1024)
        if not data:
            break
        
        line = data.decode('utf-8')
        print("Received:", repr(line))
        
        msg_type, params = parse_tcp_line(line)
        
        response = ""
        
        if msg_type == "AUTH":
            authenticated = True
            user_display_name = params["displayname"]
            current_channel = "default" 
            ROOMS[current_channel].add(user_display_name) 
            response = "REPLY OK IS Auth success.\r\n"

            extra = f"MSG FROM Server IS {user_display_name} joined default.\r\n"
            response += extra
        
        elif msg_type == "JOIN":
            if not authenticated:
                response = "REPLY NOK IS You must AUTH first.\r\n"
            else:
                ch = params["channel"]
                disp = params["displayname"]
                if disp != user_display_name:
                    # If the name does not match
                    response = "REPLY NOK IS Wrong display name.\r\n"
                else:
                    # Let's try to find the channel
                    if ch in ROOMS:
                        # Remove from previous channel if it exists
                        if current_channel and user_display_name in ROOMS[current_channel]:
                            ROOMS[current_channel].remove(user_display_name)

                        ROOMS[ch].add(user_display_name)
                        current_channel = ch
                        response = "REPLY OK IS Join success.\r\n"
                        
                        extra = f"MSG FROM Server IS {user_display_name} joined {ch}.\r\n"
                        response += extra
                    else:
                        response = "REPLY NOK IS Unknown channel.\r\n"
        
        elif msg_type == "MSG":
            if not authenticated:
                response = "REPLY NOK IS You must AUTH first.\r\n"
            else:
                from_display = params["displayname"]
                message = params["message"]
                if from_display != user_display_name:
                    response = "REPLY NOK IS DisplayName mismatch.\r\n"
                else:
                    # Here it is simplified and we will just answer
                    response = "MSG FROM Server IS I got your message!\r\n"
        
        else:
            response = "REPLY NOK IS Unknown command.\r\n"
        
        conn.send(response.encode('utf-8'))
    
    conn.close()
    s.close()

if __name__ == "__main__":
    main()
