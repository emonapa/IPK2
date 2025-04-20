# Dokumentace k projektu **ipk25chat-client**

## Rozcestník obsahu
- [1. Výkonný souhrn (Executive Summary)](#1-výkonný-souhrn-executive-summary)
- [2. Krátký teoretický základ](#2-krátký-teoretický-základ)
- [3. Struktura a architektura (Narrative)](#3-struktura-a-architektura-narrative)
- [4. Testování](#4-testování)
- [5. Rozšířená funkcionalita](#5-rozšířená-funkcionalita)
- [6. Bibliografie](#6-bibliografie)

---

## 1. Výkonný souhrn (Executive Summary)

Projekt implementuje klienta pro chatovací server používající vlastní protokol **IPK25-CHAT**. Aplikace podporuje dvě varianty transportního protokolu, a to **TCP** a **UDP**. Funkcionalita zahrnuje autentizaci, přejmenování uživatelů, připojení do kanálů, odesílání zpráv, zpracování serverových zpráv (`ERR`, `MSG`, `PING`) a ukončení komunikace (`BYE`). Specifikem UDP varianty je implementace potvrzovacího mechanismu zpráv (`CONFIRM`) a dynamická změna portu po autentizaci (`AUTH`).

Klient je napsaný v jazyce C.

---

## 2. Krátký teoretický základ

### Protokol IPK25-CHAT

Protokol IPK25-CHAT umožňuje komunikaci mezi klientem a serverem pomocí textových zpráv v TCP variantě, resp. binárních zpráv v UDP variantě. Klient po spuštění naváže spojení, provede autentizaci (`AUTH`), po níž následuje potvrzení (`REPLY`) ze strany serveru. UDP protokol vyžaduje explicitní potvrzování přijetí zpráv (`CONFIRM`) a řeší dynamickou změnu portu po úspěšné autentizaci. Naopak TCP varianta využívá spolehlivé spojení, kde není potřeba explicitních potvrzení doručení.

### Základní zprávy protokolu

- **AUTH:** autentizace klienta
- **REPLY:** odpověď serveru na požadavek
- **MSG:** zpráva mezi uživateli
- **ERR:** chyba, například nepříchod **REPLY**
- **JOIN:** připojení klienta ke kanálu
- **BYE:** ukončení spojení klientem
- **PING:** ověření dostupnosti klienta (UDP specifické)

---

## 3. Struktura a architektura (Narrative)

### Přehled souborů aplikace:

- **main.c:** parsování argumentů, výběr TCP/UDP transportu.
- **client.h:** definice konfigurace klienta (`client_config_t`).
- **tcp.c / udp.c:** implementace jednotlivých protokolů, hlavní komunikační smyčky.
- **utils.c:** pomocné funkce (DNS resolving, kruhový buffer pro ID zprávy).
- **server_tcp.py:** pomocný lokální TCP server.
- **server_udp.py:** pomocný lokální UDP server.


### Významné části implementace:

#### Dynamická změna UDP portu po AUTH
Po autentizaci klient přechází na port, ze kterého obdržel `REPLY` od serveru.

```c
client->server_addr.sin_port = source.sin_port;
```

#### Ověřování potvrzení UDP zpráv (`CONFIRM`)
UDP klient čeká na potvrzení odeslaných zpráv, případně zprávy opakovaně odesílá.

```c
for (int attempt = 0; attempt <= client->max_retries; ++attempt) {
    udp_send_message(client, packet) != 0

    // ...
    while (get_elapsed_ms(start) < client->timeout_ms) {
        udp_receive_message(client, recv_buf, sizeof(recv_buf), &source);
    }
    // ...
}

```

#### Správa `MessageID`
Každá zpráva obsahuje unikátní `MessageID`, které klient sleduje, aby nedocházelo ke zpracování duplicit.

---

## 4. Testování

Pro účely testování byly vytvořeny vlastní zjednodušené lokální UDP a TCP servery. Zároveň byl pro ověření kompatibility a chování klienta v reálném prostředí použit referenční Discord server poskytnutý zadáním. Na specifické testování dynamického přepínání portů v UDP variantě byl použit Wireshark.

- **Testovací prostředí:**  
  Běželo ve virtualizovaném Ubuntu 24.04.1 LTS s Nix. Pro UDP konektivitu byla použita školní VUT FIT VPN.
- **Co bylo testováno (obecně):**  
  Testovalo se, jestli se aplikace chová podle specifikace a zda nedochází k úniku paměti pomocí valgrindu.

### Vybrané testovací scénáře

**Test č. 1: Základní autentizace a odeslání zprávy (UDP)**

**Co bylo testováno:**  
Správná autentizace klienta a následné odeslání uživatelské zprávy.

**Důvod testování:**  
Ověření základní UDP komunikace a reakce na změnu portu.

**Jak bylo testováno:**  
Byl spuštěn klient, odeslán `/auth` příkaz a následně zpráva "hello world".

**Očekávaný výsledek:**  
Server obdržel `AUTH`, klient obdržel `CONFIRM` a `REPLY`. Po autentizaci klient správně odeslal zprávu na nový port serveru.

**Skutečný výsledek:**  
Test prošel úspěšně, komunikace fungovala dle očekávání.

---

**Test č. 2: Zpracování příkazu PING (UDP)**

**Co bylo testováno:**  
Odezva klienta na serverový `PING`.

**Důvod testování:**  
Prověření schopnosti klienta správně reagovat na kontrolní zprávy serveru.

**Jak bylo testováno:**  
Po autentizaci zaslal testovací server klientovi zprávu typu `PING`.

**Očekávaný výsledek:**  
Klient zaslal správně zpět potvrzení typu `CONFIRM`.

**Skutečný výsledek:**  
Klient odeslal správné potvrzení, test úěšně splněn.

---

**Test č. 3: Autentizace a změna uživatelského jména (TCP)**

**Co bylo testováno:**  
Funkce autentizace (`/auth`) a změna přezdívky uživatele (`/rename`) u TCP klienta.

**Důvod testování:**  
Ověření správného zpracování základních klientských příkazů.

**Jak bylo testováno:**  
Byla provedena autentizace a následná změna jména na "novi", poté odeslána zpráva na server.

**Očekávaný výsledek:**  
Server přijal zprávu s nově nastaveným jménem uživatele.

**Skutečný výsledek:**  
Server správně identifikoval uživatele pod novou přezdívkou, test byl úěšný.

---

## 5. Rozšířená funkcionalita

- **Možnost ladícího výstupu:** využití debug režimu (`DEBUG_PRINT`) pro snadné ladění pomocí `stderr`.

---

## 6. Bibliografie

- Postel, J. **RFC 768 - User Datagram Protocol**, ISI, 1980.
- Eddy, W. **RFC 9293 - Transmission Control Protocol**, 2022.
- Braden, R. **RFC 791 - Internet Protocol**, 1981.
- Zadání projektu IPK25-CHAT (VUT FIT, předmět IPK).
- Dokumentace Python modulu socket – [https://docs.python.org/3/library/socket.html](https://docs.python.org/3/library/socket.html)

---


