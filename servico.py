import json
import paho.mqtt.client as mqtt

BROKER = "test.mosquitto.org"
NOME = "hirrua"
base = f"sis1a/{NOME}/"

TOPICOS = [
    "temp", "umid", "led", "limite",
    "wifi", "rssi", "ip", "hora", "ciclos",
    "log/wifi", "log/tls", "log/sistema",
]

ROTULOS = {
    "temp": "Temperatura (C)",
    "umid": "Umidade (%)",
    "led": "LED",
    "limite": "Limite atual",
    "wifi": "Wi-Fi SSID",
    "rssi": "RSSI (dBm)",
    "ip": "IP",
    "hora": "Hora (TLS)",
    "ciclos": "Ciclos de deep sleep",
    "log/wifi": "Log Wi-Fi",
    "log/tls": "Log TLS",
    "log/sistema": "Log sistema",
}


def ao_conectar(cliente, userdata, flags, rc, *args):
    print(f"Conectado ao Mosquitto. Assinando {len(TOPICOS)} topicos em {base}\n")
    for t in TOPICOS:
        cliente.subscribe(base + t)


def ao_receber(cliente, userdata, msg):
    sub = msg.topic[len(base):]
    texto = msg.payload.decode(errors="replace")
    rotulo = ROTULOS.get(sub, sub)
    retido = " (retido)" if msg.retain else ""

    if sub.startswith("log/"):
        try:
            dados = json.loads(texto)
            texto = " | ".join(f"{k}={v}" for k, v in dados.items())
        except json.JSONDecodeError:
            pass

    print(f"  [{sub:<11}] {rotulo}: {texto}{retido}")


try:
    cliente = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)
except (AttributeError, TypeError):
    cliente = mqtt.Client()

cliente.on_connect = ao_conectar
cliente.on_message = ao_receber
cliente.connect(BROKER, 1883, 30)
cliente.loop_start()


def enviar(comando):
    cliente.publish(base + "comando", comando, retain=True)
    print(f"  [enviado] {comando}  (retido: o ESP32 aplica ao acordar)")


print("Comandos: NUMERO = novo limite | on/off = forca LED | auto = volta p/ regra | sair")
while True:
    entrada = input("> ").strip().lower()
    if entrada == "sair":
        break
    elif entrada in ("on", "off", "auto"):
        enviar("led:" + entrada.upper())
    elif entrada:
        try:
            float(entrada.replace(",", "."))
            enviar("limite:" + entrada.replace(",", "."))
        except ValueError:
            print("  comando invalido")

cliente.loop_stop()
cliente.disconnect()
print("Encerrado.")
