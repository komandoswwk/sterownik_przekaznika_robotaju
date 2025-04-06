#include <Wire.h>
#include <RTClib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <ArduinoJson.h>

// Konfiguracja sieci WiFi w trybie Access Point
const char* ap_ssid = "nazwa"; // Nazwa sieci WiFi
const char* ap_password = "haslo";   // Hasło do sieci WiFi

// Definicja pinu sterującego dzwonkiem
#define DZWONEK_PIN 13

// Struktura przechowująca dane pojedynczego wpisu harmonogramu
struct Harmonogram {
  int godzina;          // Godzina aktywacji
  int minuta;           // Minuta aktywacji
  bool aktywny;         // Czy wpis jest aktywny
  int czasDzwonienia;   // Czas trwania dzwonienia w sekundach
};

// Maksymalna liczba wpisów w harmonogramie
#define MAX_HARMONOGRAM 30
Harmonogram harmonogram[MAX_HARMONOGRAM]; // Tablica przechowująca harmonogram

// Zmienne globalne do zarządzania stanem dzwonka
bool dzwonekAktywny = false;              // Czy dzwonek jest aktualnie aktywny
unsigned long czasAktywacji = 0;          // Czas rozpoczęcia aktywacji dzwonka

// Obiekty globalne
RTC_DS3231 rtc;                           // Obiekt do obsługi zegara RTC
WebServer server(80);                     // Serwer HTTP na porcie 80
unsigned long startCzas = millis();       // Czas startu urządzenia
float temperaturaRTC = 0;                 // Temperatura odczytana z RTC
int liczbaAktywacji = 0;                  // Licznik aktywacji dzwonka
Preferences preferences;                  // Obiekt do obsługi pamięci nieulotnej

// Funkcja wczytująca harmonogram z pamięci nieulotnej
void wczytajHarmonogram() {
  preferences.begin("harmonogram", false);  // Otwórz przestrzeń nazw w pamięci
  for (int i = 0; i < MAX_HARMONOGRAM; i++) {
    String key = "harmonogram_" + String(i); // Klucz dla danego wpisu
    if (preferences.isKey(key.c_str())) {
      // Jeśli klucz istnieje, wczytaj dane do harmonogramu
      preferences.getBytes(key.c_str(), &harmonogram[i], sizeof(Harmonogram));
    } else {
      // Jeśli brak danych, ustaw wartości domyślne
      harmonogram[i] = {8, 0, true, 3};
    }
    // Sprawdzenie poprawności danych (np. zakres godzin i minut)
    if (harmonogram[i].godzina > 23 || harmonogram[i].minuta > 59) {
      harmonogram[i] = {8, 0, true, 3};
    }
  }
  preferences.end();  // Zamknij przestrzeń nazw
}

// Funkcja zapisująca harmonogram do pamięci nieulotnej
void zapiszHarmonogram() {
  preferences.begin("harmonogram", false);  // Otwórz przestrzeń nazw w pamięci
  for (int i = 0; i < MAX_HARMONOGRAM; i++) {
    String key = "harmonogram_" + String(i); // Klucz dla danego wpisu
    preferences.putBytes(key.c_str(), &harmonogram[i], sizeof(Harmonogram));
  }
  preferences.end();  // Zamknij przestrzeń nazw
  Serial.println("Harmonogram zapisany"); // Informacja o zapisaniu harmonogramu
}

String formatujCzas(DateTime teraz) {
  char buf[20];
  sprintf(buf, "%02d:%02d:%02d", teraz.hour(), teraz.minute(), teraz.second());
  return String(buf);
}

String formatujDate(DateTime teraz) {
  char buf[20];
  sprintf(buf, "%02d/%02d/%04d", teraz.day(), teraz.month(), teraz.year());
  return String(buf);
}

String formatujCzasPracy() {
  long czas = (millis() - startCzas) / 1000;
  int godz = czas / 3600;
  int min = (czas % 3600) / 60;
  int sek = czas % 60;
  char buf[20];
  sprintf(buf, "%02d:%02d:%02d", godz, min, sek);
  return String(buf);
}

// Funkcja aktywująca dzwonek na określony czas
void aktywujDzwonek(int czas) {
  digitalWrite(DZWONEK_PIN, HIGH); // Włącz dzwonek
  dzwonekAktywny = true;           // Ustaw flagę aktywności
  czasAktywacji = millis();        // Zapisz czas aktywacji
  liczbaAktywacji++;               // Zwiększ licznik aktywacji
  
  Serial.println("Dzwonek aktywny na " + String(czas) + "s");
  Serial.println("Czas aktywacji: " + String(czasAktywacji));
}

// Funkcja sprawdzająca, czy dzwonek powinien zostać wyłączony
void sprawdzCzasDzwonka() {
  if (dzwonekAktywny) {
    unsigned long aktualnyCzas = millis();
    unsigned long czasTrwania = aktualnyCzas - czasAktywacji;
    
    if (czasTrwania >= (harmonogram[0].czasDzwonienia * 1000)) {
      digitalWrite(DZWONEK_PIN, LOW); // Wyłącz dzwonek
      dzwonekAktywny = false;         // Zresetuj flagę aktywności
      Serial.println("Dzwonek wylaczony po: " + String(czasTrwania) + "ms");
    }
  }
}

void sprawdzHarmonogram() {
  DateTime teraz = rtc.now();
  
  for (int i = 0; i < MAX_HARMONOGRAM; i++) {
    if (harmonogram[i].aktywny && 
        harmonogram[i].godzina == teraz.hour() && 
        harmonogram[i].minuta == teraz.minute() && 
        teraz.second() == 0) {
      // Aktywuj tylko jeśli dzwonek nie jest już aktywny
      if (!dzwonekAktywny) {
        aktywujDzwonek(harmonogram[i].czasDzwonienia);
      }
    }
  }
}

// Funkcja obsługująca stronę główną serwera
void handleGlowna() {
  temperaturaRTC = rtc.getTemperature(); // Odczytaj temperaturę z RTC
  DateTime teraz = rtc.now();            // Pobierz aktualny czas z RTC

  // Generowanie dynamicznej strony HTML
  String html = R"=====(  
  <!DOCTYPE html>
  <html>
  <head>
    <title>Sterownik Dzwonka WK</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      /* Stylizacja strony */
      body {
        font-family: Arial, sans-serif;
        margin: 0;
        padding: 20px;
        background-color: #f5f5f5;
      }
      .container {
        max-width: 800px;
        margin: 0 auto;
      }
      .panel {
        background: white;
        border-radius: 5px;
        padding: 15px;
        margin-bottom: 20px;
        box-shadow: 0 1px 3px rgba(0,0,0,0.1);
      }
      .panel-header {
        font-weight: bold;
        font-size: 1.2em;
        margin-bottom: 10px;
        color: #2c3e50;
      }
      .row {
        display: flex;
        flex-wrap: wrap;
        margin: 0 -10px;
      }
      .col {
        flex: 1;
        min-width: 200px;
        padding: 0 10px;
        margin-bottom: 15px;
      }
      .current-time {
        font-size: 1.5em;
        font-weight: bold;
      }
      .status-indicator {
        display: inline-block;
        width: 15px;
        height: 15px;
        border-radius: 50%;
        margin-right: 8px;
      }
      .status-on { background-color: #27ae60; }
      .status-off { background-color: #e74c3c; }
      table {
        width: 100%;
        border-collapse: collapse;
      }
      th, td {
        padding: 8px;
        text-align: left;
        border-bottom: 1px solid #ddd;
      }
      input, button {
        padding: 8px;
        margin: 5px 0;
        width: 100%;
        box-sizing: border-box;
      }
      button {
        background-color: #3498db;
        color: white;
        border: none;
        border-radius: 3px;
        cursor: pointer;
      }
      button.danger {
        background-color: #e74c3c;
      }
      button.success {
        background-color: #27ae60;
      }
      .switch {
        position: relative;
        display: inline-block;
        width: 50px;
        height: 24px;
      }
      .switch input {
        opacity: 0;
        width: 0;
        height: 0;
      }
      .slider {
        position: absolute;
        cursor: pointer;
        top: 0;
        left: 0;
        right: 0;
        bottom: 0;
        background-color: #ccc;
        transition: .4s;
        border-radius: 24px;
      }
      .slider:before {
        position: absolute;
        content: "";
        height: 16px;
        width: 16px;
        left: 4px;
        bottom: 4px;
        background-color: white;
        transition: .4s;
        border-radius: 50%;
      }
      input:checked + .slider {
        background-color: #27ae60;
      }
      input:checked + .slider:before {
        transform: translateX(26px);
      }
    </style>
  </head>
  <body>
  <div class="container">
    <!-- Panel główny -->
    <div class="panel">
      <div class="panel-header">Sterownik Dzwonka</div>
      <div class="row">
        <div class="col">
          <div class="panel">
            <div class="panel-header">Aktualny czas</div>
            <div class="current-time" id="aktualnyCzas">)=====" + formatujCzas(teraz) + R"=====(</div>
            <div id="aktualnaData">)=====" + formatujDate(teraz) + R"=====(</div>
          </div>
        </div>
        <div class="col">
          <div class="panel">
            <div class="panel-header">Status</div>
            <span class="status-indicator )=====" + (dzwonekAktywny ? "status-on" : "status-off") + R"=====(" id="statusDzwonka"></span>
            <span id="tekstStatusu">)=====" + (dzwonekAktywny ? "Dzwoni" : "Wylaczony") + R"=====(</span>
            <button onclick="testujDzwonek()">Testuj dzwonek</button>
          </div>
        </div>
      </div>
    </div>

    <div class="panel">
      <div class="panel-header">Harmonogram</div>
      <table id="tabelaHarmonogramu">
        <thead>
          <tr>
            <th>#</th>
            <th>Godzina</th>
            <th>Czas (s)</th>
            <th>Status</th>
            <th>Akcje</th>
          </tr>
        </thead>
        <tbody></tbody>
      </table>
      <button onclick="dodajHarmonogram()" class="success">Dodaj pozycje</button>
    </div>

    <div class="panel">
      <div class="panel-header">Ustawienia</div>
      <div class="row">
        <div class="col">
          <div class="panel">
            <div class="panel-header">Ustaw czas RTC</div>
            <input type="date" id="ustawDate">
            <input type="time" id="ustawGodzine">
            <button onclick="ustawCzas()">Zapisz czas</button>
          </div>
        </div>
        <div class="col">
          <div class="panel">
            <div class="panel-header">System</div>
            <div>Temperatura: <span id="tempRTC">)=====" + String(temperaturaRTC, 1) + R"=====(&deg;C</span></div>
            <div>Uptime: <span id="uptime">)=====" + formatujCzasPracy() + R"=====(</span></div>
            <div>RAM: <span>)=====" + String(ESP.getFreeHeap() / 1024.0, 1) + R"=====( kB</span></div>
            <button onclick="odswiezDane()">Odswiez</button>
          </div>
        </div>
      </div>
    </div>
  </div>

  <script>
    function aktualizujCzas() {
      fetch('/pobierzczas')
        .then(r => r.json())
        .then(d => {
          document.getElementById('aktualnyCzas').innerText = 
            `${d.godzina.toString().padStart(2,'0')}:${d.minuta.toString().padStart(2,'0')}:${d.sekunda.toString().padStart(2,'0')}`;
          
          const status = document.getElementById('statusDzwonka');
          const tekst = document.getElementById('tekstStatusu');
          status.className = d.dzwonekAktywny ? 'status-indicator status-on' : 'status-indicator status-off';
          tekst.innerText = d.dzwonekAktywny ? 'Dzwoni' : 'Wylaczony';
        });
    }
    setInterval(aktualizujCzas, 500);

    function wczytajHarmonogram() {
      fetch('/pobierzharmonogram')
        .then(r => r.json())
        .then(d => {
          const tbody = document.querySelector('#tabelaHarmonogramu tbody');
          tbody.innerHTML = '';
          d.forEach((p, i) => {
            if(p.godzina === 0 && p.minuta === 0 && !p.aktywny) return;
            const row = tbody.insertRow();
            row.innerHTML = `
              <td>${i+1}</td>
              <td><input type="time" value="${p.godzina.toString().padStart(2,'0')}:${p.minuta.toString().padStart(2,'0')}"></td>
              <td><input type="number" value="${p.czasDzwonienia}" min="1" max="30"></td>
              <td><label class="switch"><input type="checkbox" ${p.aktywny?'checked':''}><span class="slider"></span></label></td>
              <td>
                <button onclick="zapiszPozycje(${i}, this)" class="success">Zapisz</button>
                <button onclick="usunPozycje(${i})" class="danger">Usun</button>
              </td>
            `;
          });
        });
    }

    function zapiszPozycje(index, btn) {
      const row = btn.parentNode.parentNode;
      const czas = row.cells[1].querySelector('input').value.split(':');
      const czasDzwonienia = row.cells[2].querySelector('input').value;
      const aktywny = row.cells[3].querySelector('input').checked;

      fetch('/aktualizuj', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
          index: index,
          pozycja: {
            godzina: parseInt(czas[0]),
            minuta: parseInt(czas[1]),
            czasDzwonienia: parseInt(czasDzwonienia),
            aktywny: aktywny
          }
        })
      }).then(r => {
        if(r.ok) alert('Zapisano!');
        else alert('Blad!');
        wczytajHarmonogram();
      });
    }

    function usunPozycje(index) {
      if(!confirm('Usunac pozycje?')) return;
      fetch('/aktualizuj', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
          index: index,
          pozycja: {godzina:0, minuta:0, czasDzwonienia:3, aktywny:false}
        })
      }).then(r => {
        if(r.ok) wczytajHarmonogram();
      });
    }

    function dodajHarmonogram() {
      fetch('/dodaj', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
          godzina: 8,
          minuta: 0,
          czasDzwonienia: 3,
          aktywny: true
        })
      }).then(r => {
        if(r.ok) {
          alert('Dodano nowa pozycje - edytuj jesli chcesz.');
          wczytajHarmonogram();
        } else {
          alert('Nie mozna dodac nowej pozycji (pelny harmonogram?)');
        }
      });
    }

    function testujDzwonek() {
      fetch('/aktualizuj', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({testujDzwonek:true})
      }).then(r => {
        if(r.ok) alert('Dzwonek testowy!');
      });
    }

    function ustawCzas() {
      const data = new Date(document.getElementById('ustawDate').value);
      const czas = document.getElementById('ustawGodzine').value.split(':');
      
      fetch('/ustawczas', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
          rok: data.getFullYear(),
          miesiac: data.getMonth() + 1,
          dzien: data.getDate(),
          godz: parseInt(czas[0]),
          min: parseInt(czas[1])
        })
      }).then(r => {
        if(r.ok) {
          alert('Czas ustawiony!');
          aktualizujCzas();
        } else {
          alert('Blad!');
        }
      });
    }

    function odswiezDane() {
      fetch('/diagnostyka')
        .then(r => r.json())
        .then(d => {
          document.getElementById('tempRTC').innerText = d.tempRTC + '°C';
          document.getElementById('uptime').innerText = 
            `${Math.floor(d.uptime/3600).toString().padStart(2,'0')}:` +
            `${Math.floor((d.uptime%3600)/60).toString().padStart(2,'0')}:` +
            `${(d.uptime%60).toString().padStart(2,'0')}`;
        });
    }

    // Inicjalizacja
    window.onload = function() {
      const today = new Date();
      document.getElementById('ustawDate').value = 
        `${today.getFullYear()}-${(today.getMonth()+1).toString().padStart(2,'0')}-${today.getDate().toString().padStart(2,'0')}`;
      document.getElementById('ustawGodzine').value = 
        `${today.getHours().toString().padStart(2,'0')}:${today.getMinutes().toString().padStart(2,'0')}`;
      
      aktualizujCzas();
      wczytajHarmonogram();
      setInterval(aktualizujCzas, 1000);
    };
  </script>
  </div>
    <div class="footer">
      Sterownik Dzwonka WK | Projekt i realizacja: <b>//WK</b> | Wersja 1.0
    </div>
</body>
</html>
  )=====";

  server.send(200, "text/html", html); // Wyślij stronę HTML do klienta
}

// Funkcja obsługująca żądanie aktualizacji danych (np. harmonogramu lub testu dzwonka)
void handleAktualizuj() {
  if (server.method() == HTTP_POST) { // Obsługiwane tylko żądania POST
    String body = server.arg("plain"); // Pobierz treść żądania

    // Sprawdź, czy żądanie dotyczy testu dzwonka
    if (body.indexOf("\"testujDzwonek\":true") != -1) {
      aktywujDzwonek(3); // Aktywuj dzwonek na 3 sekundy
      server.send(200, "text/plain", "OK"); // Wyślij odpowiedź
      return;
    }

    // Parsowanie JSON z treści żądania
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
      server.send(400, "text/plain", "Nieprawidłowy JSON"); // Błąd parsowania
      return;
    }

    // Pobierz dane z JSON
    int index = doc["index"];
    int godzina = doc["pozycja"]["godzina"];
    int minuta = doc["pozycja"]["minuta"];
    bool aktywny = doc["pozycja"]["aktywny"];
    int czasDzwonienia = doc["pozycja"]["czasDzwonienia"];

    // Zaktualizuj wpis w harmonogramie, jeśli indeks jest poprawny
    if (index >= 0 && index < MAX_HARMONOGRAM) {
      harmonogram[index] = {godzina, minuta, aktywny, czasDzwonienia};
      zapiszHarmonogram(); // Zapisz zmiany do pamięci
      server.send(200, "text/plain", "OK"); // Wyślij odpowiedź
    } else {
      server.send(400, "text/plain", "Nieprawidłowy index"); // Błąd indeksu
    }
  } else {
    server.send(405, "text/plain", "Metoda niedozwolona"); // Obsługiwane tylko POST
  }
}

// Funkcja obsługująca żądanie pobrania aktualnego czasu z RTC
void handlePobierzCzas() {
  DateTime teraz = rtc.now(); // Pobierz aktualny czas z RTC
  String json = "{";
  json += "\"godzina\":" + String(teraz.hour()) + ",";
  json += "\"minuta\":" + String(teraz.minute()) + ",";
  json += "\"sekunda\":" + String(teraz.second()) + ",";
  json += "\"dzien\":" + String(teraz.day()) + ",";
  json += "\"miesiac\":" + String(teraz.month()) + ",";
  json += "\"rok\":" + String(teraz.year()) + ",";
  json += "\"dzwonekAktywny\":" + String(dzwonekAktywny ? "true" : "false");
  json += "}";

  server.send(200, "application/json", json); // Wyślij dane w formacie JSON
}

// Funkcja obsługująca żądanie pobrania harmonogramu
void handlePobierzHarmonogram() {
  String json = "[";
  for (int i = 0; i < MAX_HARMONOGRAM; i++) {
    if (i > 0) json += ","; // Dodaj przecinek między wpisami
    json += "{";
    json += "\"godzina\":" + String(harmonogram[i].godzina) + ",";
    json += "\"minuta\":" + String(harmonogram[i].minuta) + ",";
    json += "\"aktywny\":" + String(harmonogram[i].aktywny ? "true" : "false") + ",";
    json += "\"czasDzwonienia\":" + String(harmonogram[i].czasDzwonienia);
    json += "}";
  }
  json += "]";

  server.send(200, "application/json", json); // Wyślij dane w formacie JSON
}

// Funkcja obsługująca ustawienie czasu RTC
void handleUstawCzas() {
  if (server.method() == HTTP_POST) { // Obsługiwane tylko żądania POST
    String body = server.arg("plain"); // Pobierz treść żądania

    // Pobierz dane z JSON
    int rok = body.substring(body.indexOf("\"rok\":") + 6, body.indexOf(",")).toInt();
    int miesiac = body.substring(body.indexOf("\"miesiac\":") + 10, body.indexOf(",", body.indexOf("\"miesiac\":"))).toInt();
    int dzien = body.substring(body.indexOf("\"dzien\":") + 8, body.indexOf(",", body.indexOf("\"dzien\":"))).toInt();
    int godz = body.substring(body.indexOf("\"godz\":") + 7, body.indexOf(",", body.indexOf("\"godz\":"))).toInt();
    int min = body.substring(body.indexOf("\"min\":") + 6, body.indexOf("}")).toInt();

    rtc.adjust(DateTime(rok, miesiac, dzien, godz, min, 0)); // Ustaw czas RTC
    server.send(200, "text/plain", "OK"); // Wyślij odpowiedź
  } else {
    server.send(405, "text/plain", "Metoda niedozwolona"); // Obsługiwane tylko POST
  }
}

// Funkcja obsługująca diagnostykę systemu (np. temperatura RTC, uptime, RAM)
void handleDiagnostyka() {
  String json = "{";
  json += "\"tempRTC\":" + String(rtc.getTemperature(), 1) + ","; // Temperatura RTC
  json += "\"uptime\":" + String((millis() - startCzas) / 1000) + ","; // Czas pracy w sekundach
  json += "\"ram\":" + String(ESP.getFreeHeap() / 1024.0, 1); // Dostępna pamięć RAM w kB
  json += "}";

  server.send(200, "application/json", json); // Wyślij dane w formacie JSON
}

// Funkcja obsługująca dodanie nowej pozycji do harmonogramu
void handleDodajPozycje() {
  if (server.method() == HTTP_POST) { // Obsługiwane tylko żądania POST
    String body = server.arg("plain"); // Pobierz treść żądania

    // Pobierz dane z JSON lub ustaw wartości domyślne
    int godzina = body.indexOf("\"godzina\":") >= 0 ? body.substring(body.indexOf("\"godzina\":") + 10, body.indexOf(",", body.indexOf("\"godzina\":"))).toInt() : 8;
    int minuta = body.indexOf("\"minuta\":") >= 0 ? body.substring(body.indexOf("\"minuta\":") + 9, body.indexOf(",", body.indexOf("\"minuta\":"))).toInt() : 0;
    int czasDzwonienia = body.indexOf("\"czasDzwonienia\":") >= 0 ? body.substring(body.indexOf("\"czasDzwonienia\":") + 16, body.indexOf("}", body.indexOf("\"czasDzwonienia\":"))).toInt() : 3;
    bool aktywny = body.indexOf("\"aktywny\":true") != -1;

    // Znajdź pierwsze wolne miejsce w harmonogramie
    for (int i = 0; i < MAX_HARMONOGRAM; i++) {
      if (!harmonogram[i].aktywny && harmonogram[i].godzina == 0 && harmonogram[i].minuta == 0) {
        harmonogram[i] = {godzina, minuta, aktywny, czasDzwonienia}; // Dodaj nową pozycję
        zapiszHarmonogram(); // Zapisz zmiany do pamięci
        server.send(200, "text/plain", "Dodano"); // Wyślij odpowiedź
        return;
      }
    }

    server.send(400, "text/plain", "Brak miejsca"); // Brak miejsca w harmonogramie
  } else {
    server.send(405, "text/plain", "Metoda niedozwolona"); // Obsługiwane tylko POST
  }
}

// Funkcja inicjalizująca urządzenie
void setup() {
  Serial.begin(115200); // Rozpocznij komunikację przez UART
  pinMode(DZWONEK_PIN, OUTPUT); // Ustaw pin dzwonka jako wyjście
  digitalWrite(DZWONEK_PIN, LOW); // Ustaw stan niski na pinie dzwonka

  Wire.begin(); // Inicjalizacja magistrali I2C
  if (!rtc.begin()) {
    Serial.println("Blad RTC!"); // Błąd inicjalizacji RTC
    while (1); // Zatrzymaj program
  }

  if (rtc.lostPower()) {
    Serial.println("RTC bez zasilania"); // RTC stracił zasilanie
    //rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // Opcjonalne ustawienie czasu
  }

  esp_task_wdt_init(30, true); // Inicjalizacja Watchdog Timer
  preferences.begin("harmonogram", false); // Otwórz przestrzeń pamięci
  wczytajHarmonogram(); // Wczytaj harmonogram z pamięci
  preferences.end(); // Zamknij przestrzeń pamięci

  WiFi.softAP(ap_ssid, ap_password); // Uruchom Access Point
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP()); // Wyświetl adres IP

  // Rejestracja obsługi żądań HTTP
  server.on("/", handleGlowna);
  server.on("/aktualizuj", handleAktualizuj);
  server.on("/pobierzczas", handlePobierzCzas);
  server.on("/pobierzharmonogram", handlePobierzHarmonogram);
  server.on("/ustawczas", HTTP_POST, handleUstawCzas);
  server.on("/diagnostyka", handleDiagnostyka);
  server.on("/dodaj", HTTP_POST, handleDodajPozycje);
  server.begin(); // Uruchom serwer HTTP
}

// Funkcja głównej pętli programu
void loop() {
  server.handleClient(); // Obsługa żądań HTTP
  sprawdzHarmonogram(); // Sprawdź harmonogram
  sprawdzCzasDzwonka(); // Sprawdź stan dzwonka
  esp_task_wdt_reset(); // Zresetuj Watchdog Timer
}
