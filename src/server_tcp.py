import socket

# Předdefinované roomky (kanály)
ROOMS = {
    "default": set(),
    "room1": set(),
    "room2": set()
}

def parse_tcp_line(line: str):
    """
    Velmi zjednodušený parser pro příchozí příkaz.
    Příklad:
     - "AUTH username AS displayName USING secret\r\n"
     - "JOIN channelName AS displayName\r\n"
     - "MSG FROM displayName IS obsah zprávy\r\n"
    Vrací tuple (typ_zpravy, parametry_dict).
    Pokud se nepodaří rozpoznat, vrátíme ('UNKNOWN', {}).
    """
    line = line.strip('\r\n')
    if line.startswith("AUTH "):
        # Očekáváme formát: AUTH username AS displayName USING secret
        # Rozdělíme si to jen hrubou parse, tady extrémně zjednodušeně:
        # (Neřešíme případy, kdy tam parametry nejsou, atd.)
        try:
            # Příklad: line = "AUTH user AS Nick USING pass"
            # Rozsekáme:
            #   0: AUTH
            #   1: user
            #   2: AS
            #   3: Nick
            #   4: USING
            #   5: pass
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
            # Příklad: "JOIN room1 AS Uzivatel"
            # Rozsekáme hrubě
            parts = line.split()
            channel = parts[1]
            # parts[2] = "AS"
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
            # Tohle je jen velmi hrubé a neohrabané rozebrání textu
            # Rozdělíme jednou na "MSG FROM " a zbytek
            after_from = line[len("MSG FROM "):]
            # Najdeme " IS "
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
            # Ověříme - tady to jen "schválíme"
            authenticated = True
            user_display_name = params["displayname"]
            current_channel = "default"  # Po "AUTH" ho dáme do default
            ROOMS[current_channel].add(user_display_name)  # Přidáme do default kanálu
            response = "REPLY OK IS Auth success.\r\n"
            # Ještě můžeme poslat MSG FROM Server IS x joined default
            # (Ale to tu necháme jen v jedné odpovědi)
            extra = f"MSG FROM Server IS {user_display_name} joined default.\r\n"
            response += extra
        
        elif msg_type == "JOIN":
            if not authenticated:
                response = "REPLY NOK IS You must AUTH first.\r\n"
            else:
                ch = params["channel"]
                disp = params["displayname"]
                if disp != user_display_name:
                    # Kdyby se neshodlo jméno
                    response = "REPLY NOK IS Wrong display name.\r\n"
                else:
                    # Zkusíme najít channel
                    if ch in ROOMS:
                        # Odstranit z předchozího kanálu, pokud existuje
                        if current_channel and user_display_name in ROOMS[current_channel]:
                            ROOMS[current_channel].remove(user_display_name)
                        # Přidáme do nového
                        ROOMS[ch].add(user_display_name)
                        current_channel = ch
                        response = "REPLY OK IS Join success.\r\n"
                        # plus “serverová” MSG
                        extra = f"MSG FROM Server IS {user_display_name} joined {ch}.\r\n"
                        response += extra
                    else:
                        response = "REPLY NOK IS Unknown channel.\r\n"
        
        elif msg_type == "MSG":
            if not authenticated:
                response = "REPLY NOK IS You must AUTH first.\r\n"
            else:
                # Zde se třeba jen potvrdí, v reálné implementaci by se broadcastlo do kanálu
                from_display = params["displayname"]
                message = params["message"]
                if from_display != user_display_name:
                    response = "REPLY NOK IS DisplayName mismatch.\r\n"
                else:
                    # Tady je to zjednodušené a jen odpovíme
                    response = "MSG FROM Server IS I got your message!\r\n"
        
        else:
            # Neznáme příkaz
            response = "REPLY NOK IS Unknown command.\r\n"
        
        # Odeslání odpovědi
        conn.send(response.encode('utf-8'))
    
    conn.close()
    s.close()

if __name__ == "__main__":
    main()
