Smart Home Sensor ESP32

Die Sensor-Komponente ist Teil eines Smart-Home-Projekts und dient der Erfassung, Speicherung und Übertragung von Umweltdaten. Ziel des Prototyps ist es, die im Projekt definierten Anforderungen vollständig umzusetzen und eine zuverlässige Kommunikation mit weiteren Systemkomponenten, insbesondere der Display-Komponente, zu ermöglichen. Als zentrale Steuereinheit kommt ein ESP32-Entwicklungsboard von AZ-Delivery zum Einsatz, das sowohl die Sensoransteuerung als auch die WLAN- und MQTT-Kommunikation übernimmt.

Funktionsweise

Zur Messung der Umweltdaten wird ein BME280-Sensor verwendet, der über die I2C-Schnittstelle angebunden ist. Der Sensor liefert kontinuierlich Messwerte für Temperatur, Luftfeuchtigkeit und Luftdruck. Diese Daten werden in festen Zeitintervallen vom ESP32 ausgelesen, verarbeitet und im JSON-Format über das MQTT-Protokoll an einen konfigurierbaren MQTT-Broker übertragen. Dadurch können andere Komponenten des Smart-Home-Systems die Messwerte in Echtzeit empfangen und weiterverarbeiten.

Neben der Übertragung der Sensordaten erfolgt zusätzlich eine lokale Speicherung auf einer Micro-SD-Karte. Hierzu ist ein SD-Kartenmodul über die SPI-Schnittstelle mit dem ESP32 verbunden. Alle gesendeten Messwerte werden auf der SD-Karte abgelegt, sodass eine spätere Auswertung oder Analyse der Daten auch unabhängig von der Netzwerkverbindung möglich ist. Die SD-Karte ist dabei so vorgesehen, dass sie über einen Slot im Gehäuse einfach entnommen werden kann.

Die Einbindung der Sensor-Komponente in ein bestehendes WLAN-Netzwerk erfolgt über WiFi-Provisioning. Nach der erstmaligen Konfiguration verbindet sich der ESP32 automatisch mit dem WLAN. Sollte die Verbindung unterbrochen werden, stellt das System selbstständig eine erneute Verbindung her. Die Adresse des MQTT-Brokers ist flexibel konfigurierbar und kann während des Betriebs geändert werden, ohne dass der ESP32 neu programmiert werden muss. Zugangsdaten und andere sensible Informationen werden dabei sicher im internen NVS-Speicher des ESP32 abgelegt und nicht im Quellcode gespeichert.

Für die Kommunikation zwischen den einzelnen Smart-Home-Komponenten wird ein MQTT-Broker eingesetzt, beispielsweise ein lokal betriebener Mosquitto-Broker oder ein Cloud-Dienst wie Adafruit IO. Zur Fehlersuche und Systemüberwachung nutzt die Sensor-Komponente die UART-Schnittstelle, über die Status- und Debug-Informationen ausgegeben werden. Zusätzlich erfolgt eine visuelle Rückmeldung über den aktuellen Systemzustand mithilfe von zwei LEDs. Eine grüne LED signalisiert den normalen Betrieb sowie eine aktive WLAN- und MQTT-Verbindung, während eine rote LED auf Fehlerzustände, wie Verbindungsprobleme, hinweist.

Hinweise und Ausblick

Die Sensor-Komponente ist modular aufgebaut und so konzipiert, dass zukünftige Erweiterungen problemlos umgesetzt werden können. Dazu zählen beispielsweise zusätzliche Sensoren, neue Kommunikationsfunktionen oder eine erweiterte Fehlerauswertung. Der Betrieb ist sowohl stationär als auch mobil möglich, da das System für den Einsatz mit einer Akkustromversorgung ausgelegt ist. Um den Energieverbrauch zu minimieren, ist ein Energiemanagement vorgesehen, das unter anderem den Einsatz von Sleep-Modi ermöglicht.

Autor
Niklas Bräu
