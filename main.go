// StageWatch : envoie des messages courts à des montres sur scène.
// Un seul exécutable : hub WebSocket + pages /admin, /public, /sim.
package main

import (
	"embed"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"io/fs"
	"log"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strconv"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

//go:embed web
var webFS embed.FS

const (
	maxText      = 96
	pongWait     = 30 * time.Second
	pingInterval = 10 * time.Second
	writeWait    = 5 * time.Second
	maxWatches   = 8
)

// ---------- Modèle ----------

type Message struct {
	MsgID int64  `json:"msgId"`
	To    int    `json:"to"`
	Text  string `json:"text"`
	Time  int64  `json:"time"` // ms epoch
	Acked bool   `json:"acked"`
	AckAt int64  `json:"ackAt,omitempty"`
	// Pending : retenu côté serveur jusqu'à la fin de l'animation public (mode révélation)
	Pending bool `json:"pending,omitempty"`
}

type Watch struct {
	ID         int    `json:"id"`
	Name       string `json:"name"`
	Online     bool   `json:"online"`
	Battery    int    `json:"battery"` // -1 inconnu
	MilliV     int    `json:"mv"`      // tension batterie (mV), 0 = inconnue
	USB        bool   `json:"usb"`     // câble branché
	Charging   bool   `json:"chg"`     // en charge
	FW         string `json:"fw"`      // version du firmware
	lastBatLog int64
	LastSeen   int64    `json:"lastSeen"`
	Last       *Message `json:"last"`
	Sim        bool     `json:"sim"`
	Photo      string   `json:"photo"`
	conn       *client
}

type Config struct {
	Names  map[string]string `json:"names"`
	Photos map[string]int64  `json:"photos"` // id -> version (anti-cache)
	// Animation de la page public à chaque envoi
	Animate *bool `json:"animate"`
	// Révélation : la montre reçoit le message seulement quand il « atterrit » sur la page public
	Reveal bool `json:"reveal"`
}

type client struct {
	conn *websocket.Conn
	send chan []byte
	role string
	id   int
}

type Hub struct {
	mu      sync.Mutex
	watches map[int]*Watch
	viewers map[*client]bool // admin + public
	history []Message
	nextID  int64
	// fin prévue de la dernière animation public planifiée (ms epoch)
	animBusyUntil int64
	cfg           Config
	cfgPath       string
	dataDir       string
	logFile       *os.File
}

func newHub(dataDir string) *Hub {
	h := &Hub{watches: map[int]*Watch{}, viewers: map[*client]bool{}, nextID: time.Now().UnixMilli()}
	h.cfgPath = filepath.Join(dataDir, "stagewatch-config.json")
	h.dataDir = dataDir
	h.cfg = Config{Names: map[string]string{}, Photos: map[string]int64{}}
	if b, err := os.ReadFile(h.cfgPath); err == nil {
		_ = json.Unmarshal(b, &h.cfg)
		if h.cfg.Names == nil {
			h.cfg.Names = map[string]string{}
		}
		if h.cfg.Photos == nil {
			h.cfg.Photos = map[string]int64{}
		}
	}
	for i := 1; i <= maxWatches; i++ {
		h.watches[i] = &Watch{ID: i, Name: h.nameOf(i), Battery: -1, Photo: h.photoOf(i)}
	}
	_ = os.MkdirAll(filepath.Join(dataDir, "logs"), 0o755)
	lp := filepath.Join(dataDir, "logs", "show-"+time.Now().Format("2006-01-02")+".log")
	if f, err := os.OpenFile(lp, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o644); err == nil {
		h.logFile = f
	}
	return h
}

func (h *Hub) nameOf(id int) string {
	if n, ok := h.cfg.Names[strconv.Itoa(id)]; ok && n != "" {
		return n
	}
	return fmt.Sprintf("Participant %d", id)
}

func (h *Hub) photoOf(id int) string {
	if v, ok := h.cfg.Photos[strconv.Itoa(id)]; ok {
		return fmt.Sprintf("/photo/%d?v=%d", id, v)
	}
	return ""
}

func (h *Hub) photoPath(id int) string {
	return filepath.Join(h.dataDir, "photos", strconv.Itoa(id)+".img")
}

// POST (corps = image, déjà réduite par l'admin) ou DELETE
func (h *Hub) servePhotoAPI(w http.ResponseWriter, r *http.Request) {
	id, err := strconv.Atoi(r.URL.Query().Get("id"))
	if err != nil || id < 1 || id > maxWatches {
		http.Error(w, "id invalide", 400)
		return
	}
	switch r.Method {
	case http.MethodPost:
		b, err := io.ReadAll(io.LimitReader(r.Body, 5<<20))
		if err != nil || len(b) == 0 {
			http.Error(w, "image invalide", 400)
			return
		}
		ct := http.DetectContentType(b)
		if ct != "image/jpeg" && ct != "image/png" && ct != "image/webp" {
			http.Error(w, "format non supporté", 400)
			return
		}
		_ = os.MkdirAll(filepath.Join(h.dataDir, "photos"), 0o755)
		if err := os.WriteFile(h.photoPath(id), b, 0o644); err != nil {
			http.Error(w, err.Error(), 500)
			return
		}
		h.mu.Lock()
		h.cfg.Photos[strconv.Itoa(id)] = time.Now().UnixMilli()
	case http.MethodDelete:
		_ = os.Remove(h.photoPath(id))
		h.mu.Lock()
		delete(h.cfg.Photos, strconv.Itoa(id))
	default:
		http.Error(w, "méthode non autorisée", 405)
		return
	}
	h.watches[id].Photo = h.photoOf(id)
	h.saveCfg()
	h.pushState()
	h.mu.Unlock()
	w.WriteHeader(204)
}

func (h *Hub) servePhoto(w http.ResponseWriter, r *http.Request) {
	id, err := strconv.Atoi(filepath.Base(r.URL.Path))
	if err != nil || id < 1 || id > maxWatches {
		http.NotFound(w, r)
		return
	}
	b, err := os.ReadFile(h.photoPath(id))
	if err != nil {
		http.NotFound(w, r)
		return
	}
	w.Header().Set("Content-Type", http.DetectContentType(b))
	w.Header().Set("Cache-Control", "max-age=31536000")
	w.Write(b)
}

func (h *Hub) saveCfg() {
	b, _ := json.MarshalIndent(h.cfg, "", "  ")
	_ = os.WriteFile(h.cfgPath, b, 0o644)
}

func (h *Hub) logf(format string, a ...any) {
	line := time.Now().Format("15:04:05") + " " + fmt.Sprintf(format, a...)
	log.Println(line)
	if h.logFile != nil {
		fmt.Fprintln(h.logFile, line)
	}
}

func enc(v any) []byte { b, _ := json.Marshal(v); return b }

func (c *client) push(b []byte) {
	select {
	case c.send <- b:
	default: // client trop lent : on le laisse tomber plutôt que bloquer le hub
	}
}

// à appeler avec h.mu verrouillé
func (h *Hub) broadcastViewers(v any) {
	b := enc(v)
	for c := range h.viewers {
		c.push(b)
	}
}

func (h *Hub) stateLocked() map[string]any {
	ids := make([]int, 0, len(h.watches))
	for id := range h.watches {
		ids = append(ids, id)
	}
	sort.Ints(ids)
	list := make([]*Watch, 0, len(ids))
	for _, id := range ids {
		list = append(list, h.watches[id])
	}
	hist := h.history
	if len(hist) > 50 {
		hist = hist[len(hist)-50:]
	}
	return map[string]any{"type": "state", "watches": list, "history": hist,
		"settings": map[string]bool{"animate": h.animate(), "reveal": h.cfg.Reveal}}
}

func (h *Hub) pushState() { h.broadcastViewers(h.stateLocked()) }

// ---------- Logique ----------

// Durées de l'animation public (ms). Le serveur est l'horloge de référence :
// il planifie chaque envoi dans une file, et la page public suit ce planning.
const (
	animNormal = 6000 // durée normale (révélation + vol + atterrissage)
	animFast   = 3000 // durée accélérée quand des envois attendent dans la file
	animGap    = 300  // petite pause entre deux animations
)

func (h *Hub) animate() bool { return h.cfg.Animate == nil || *h.cfg.Animate }

func (h *Hub) sendText(targets []int, text string) {
	h.mu.Lock()
	defer h.mu.Unlock()
	nowT := time.Now()
	now := nowT.UnixMilli()

	// Planification dans la file d'animation
	delay, dur := int64(0), int64(0)
	if h.animate() {
		start := now
		if h.animBusyUntil > now {
			start = h.animBusyUntil
		}
		delay = start - now
		dur = animNormal
		if delay > 0 {
			dur = animFast // déjà du retard : on accélère
		}
		h.animBusyUntil = start + dur + animGap
	}
	reveal := h.animate() && h.cfg.Reveal
	landing := time.Duration(delay+dur) * time.Millisecond

	h.nextID++
	batchID := h.nextID
	ids := []int{}
	for _, id := range targets {
		w, ok := h.watches[id]
		if !ok {
			continue
		}
		ids = append(ids, id)
		h.nextID++
		m := &Message{MsgID: h.nextID, To: id, Text: text, Time: now, Pending: reveal}
		w.Last = m
		h.history = append(h.history, *m)
		if reveal {
			h.logf("ENVOI   -> %d (%s) : %q  [révélation : montre dans %.1f s]", id, w.Name, text, landing.Seconds())
			wid, mid := id, m.MsgID
			time.AfterFunc(landing, func() { h.deliverHeld(wid, mid) })
		} else {
			h.deliver(w)
			h.logf("ENVOI   -> %d (%s) : %q%s", id, w.Name, text, map[bool]string{true: "", false: "  [montre hors ligne, sera livré à la reconnexion]"}[w.conn != nil])
		}
	}
	if len(ids) == 0 {
		return
	}
	h.broadcastViewers(map[string]any{"type": "send", "batch": map[string]any{
		"id": batchID, "to": ids, "text": text, "delay": delay, "dur": dur, "reveal": reveal}})
	if len(h.history) > 500 {
		h.history = h.history[len(h.history)-500:]
	}
	h.pushState()
}

// à appeler avec h.mu verrouillé
func (h *Hub) deliver(w *Watch) {
	if w.conn != nil && w.Last != nil {
		w.conn.push(enc(map[string]any{"type": "msg", "msgId": w.Last.MsgID, "text": w.Last.Text}))
	}
}

// Fin de l'animation en mode révélation : on libère le message vers la montre.
// Chaque message retenu est livré, même si un autre a été envoyé entre-temps (la montre les reçoit dans l'ordre).
// « Effacer l'écran » annule les messages encore retenus.
func (h *Hub) deliverHeld(id int, msgID int64) {
	h.mu.Lock()
	defer h.mu.Unlock()
	w := h.watches[id]
	var m *Message
	for i := len(h.history) - 1; i >= 0; i-- {
		if h.history[i].MsgID == msgID {
			m = &h.history[i]
			break
		}
	}
	if m == nil || !m.Pending {
		return
	}
	m.Pending = false
	if w.Last != nil && w.Last.MsgID == msgID {
		w.Last.Pending = false
	}
	if w.conn != nil {
		w.conn.push(enc(map[string]any{"type": "msg", "msgId": m.MsgID, "text": m.Text}))
	}
	h.logf("RÉVÉLÉ  -> %d (%s)%s", id, w.Name, map[bool]string{true: "", false: "  [montre hors ligne, sera livré à la reconnexion]"}[w.conn != nil])
	h.pushState()
}

func (h *Hub) clearWatches(targets []int) {
	h.mu.Lock()
	defer h.mu.Unlock()
	for _, id := range targets {
		for i := range h.history {
			if h.history[i].To == id && h.history[i].Pending {
				h.history[i].Pending = false // révélation annulée
			}
		}
		if w, ok := h.watches[id]; ok {
			w.Last = nil
			if w.conn != nil {
				w.conn.push(enc(map[string]any{"type": "clear"}))
			}
		}
	}
	h.broadcastViewers(map[string]any{"type": "cleared", "ids": targets})
	h.pushState()
}

// à appeler avec h.mu verrouillé. Journal batterie toutes les 5 min (pour mesurer l'autonomie).
func (h *Hub) setPower(w *Watch, m inMsg, battery int) {
	w.Battery, w.MilliV, w.USB, w.Charging = battery, m.MV, m.USB, m.Chg
	if now := time.Now().UnixMilli(); !w.Sim && m.MV > 0 && now-w.lastBatLog >= 5*60*1000 {
		w.lastBatLog = now
		h.logf("BATTERIE montre %d : %d%% %.2fV%s", w.ID, battery, float64(m.MV)/1000,
			map[bool]string{true: " (USB branché)", false: " (sur batterie)"}[m.USB])
	}
}

func (h *Hub) watchHello(c *client, id, battery int, sim bool, m inMsg) {
	h.mu.Lock()
	defer h.mu.Unlock()
	w := h.watches[id]
	if w.conn != nil && w.conn != c {
		// une autre montre avec le même ID : on garde la plus récente
		h.logf("ATTENTION : deux montres annoncent l'ID %d, la plus récente remplace l'ancienne", id)
		w.conn.conn.Close()
	}
	c.id = id
	w.conn = c
	w.Online = true
	w.Sim = sim
	w.FW = m.FW
	w.lastBatLog = 0
	h.setPower(w, m, battery)
	w.LastSeen = time.Now().UnixMilli()
	h.logf("CONNEXION montre %d (%s)%s%s", id, w.Name, map[bool]string{true: " [simulateur]", false: ""}[sim],
		map[bool]string{true: " firmware " + m.FW, false: ""}[m.FW != ""])
	// Re-livre le dernier message s'il n'a pas été confirmé
	if w.Last != nil && !w.Last.Acked && !w.Last.Pending {
		c.push(enc(map[string]any{"type": "msg", "msgId": w.Last.MsgID, "text": w.Last.Text}))
	}
	h.pushState()
}

func (h *Hub) watchGone(c *client) {
	h.mu.Lock()
	defer h.mu.Unlock()
	if w, ok := h.watches[c.id]; ok && w.conn == c {
		w.conn = nil
		w.Online = false
		h.logf("DÉCONNEXION montre %d (%s)", w.ID, w.Name)
		h.pushState()
	}
}

func (h *Hub) watchAck(c *client, msgID int64) {
	h.mu.Lock()
	defer h.mu.Unlock()
	w, ok := h.watches[c.id]
	if !ok {
		return
	}
	now := time.Now().UnixMilli()
	if w.Last != nil && w.Last.MsgID == msgID {
		w.Last.Acked = true
		w.Last.AckAt = now
	}
	for i := len(h.history) - 1; i >= 0; i-- {
		if h.history[i].MsgID == msgID {
			h.history[i].Acked = true
			h.history[i].AckAt = now
			break
		}
	}
	h.logf("REÇU    <- %d (%s) msg %d", w.ID, w.Name, msgID)
	h.broadcastViewers(map[string]any{"type": "ack", "msgId": msgID, "id": w.ID})
	h.pushState()
}

func (h *Hub) watchBeat(c *client, battery int, m inMsg) {
	h.mu.Lock()
	defer h.mu.Unlock()
	if w, ok := h.watches[c.id]; ok && w.conn == c {
		h.setPower(w, m, battery)
		w.LastSeen = time.Now().UnixMilli()
		h.pushState()
	}
}

// ---------- WebSocket ----------

var upgrader = websocket.Upgrader{
	ReadBufferSize: 1024, WriteBufferSize: 1024,
	CheckOrigin: func(r *http.Request) bool { return true }, // réseau local fermé
}

type inMsg struct {
	Type    string          `json:"type"`
	ID      int             `json:"id"`
	Battery *int            `json:"battery"`
	MsgID   int64           `json:"msgId"`
	Sim     bool            `json:"sim"`
	MV      int             `json:"mv"`
	USB     bool            `json:"usb"`
	Chg     bool            `json:"chg"`
	FW      string          `json:"fw"`
	Targets json.RawMessage `json:"targets"`
	Text    string          `json:"text"`
	Name    string          `json:"name"`
	Animate bool            `json:"animate"`
	Reveal  bool            `json:"reveal"`
}

func (h *Hub) resolveTargets(raw json.RawMessage) []int {
	var s string
	if json.Unmarshal(raw, &s) == nil && s == "all" {
		h.mu.Lock()
		ids := []int{}
		for id := range h.watches {
			ids = append(ids, id)
		}
		h.mu.Unlock()
		sort.Ints(ids)
		return ids
	}
	var ids []int
	_ = json.Unmarshal(raw, &ids)
	return ids
}

func (h *Hub) serveWS(w http.ResponseWriter, r *http.Request) {
	role := r.URL.Query().Get("role")
	if role != "watch" && role != "admin" && role != "public" {
		http.Error(w, "role invalide", 400)
		return
	}
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		return
	}
	c := &client{conn: conn, send: make(chan []byte, 64), role: role}

	if role != "watch" {
		h.mu.Lock()
		h.viewers[c] = true
		c.push(enc(h.stateLocked()))
		h.mu.Unlock()
	}

	// Écriture
	done := make(chan struct{})
	go func() {
		t := time.NewTicker(pingInterval)
		defer func() { t.Stop(); conn.Close() }()
		for {
			select {
			case b := <-c.send:
				conn.SetWriteDeadline(time.Now().Add(writeWait))
				if conn.WriteMessage(websocket.TextMessage, b) != nil {
					return
				}
			case <-t.C:
				conn.SetWriteDeadline(time.Now().Add(writeWait))
				if conn.WriteMessage(websocket.PingMessage, nil) != nil {
					return
				}
			case <-done:
				return
			}
		}
	}()

	// Lecture
	conn.SetReadLimit(4096)
	conn.SetReadDeadline(time.Now().Add(pongWait))
	conn.SetPongHandler(func(string) error { conn.SetReadDeadline(time.Now().Add(pongWait)); return nil })
	for {
		_, data, err := conn.ReadMessage()
		if err != nil {
			break
		}
		conn.SetReadDeadline(time.Now().Add(pongWait))
		var m inMsg
		if json.Unmarshal(data, &m) != nil {
			continue
		}
		h.handle(c, m)
	}
	close(done)
	if role == "watch" {
		h.watchGone(c)
	} else {
		h.mu.Lock()
		delete(h.viewers, c)
		h.mu.Unlock()
	}
}

func (h *Hub) handle(c *client, m inMsg) {
	bat := -1
	if m.Battery != nil {
		bat = *m.Battery
	}
	switch c.role {
	case "watch":
		switch m.Type {
		case "hello":
			if m.ID >= 1 && m.ID <= maxWatches {
				h.watchHello(c, m.ID, bat, m.Sim, m)
			}
		case "hb":
			h.watchBeat(c, bat, m)
		case "ack":
			h.watchAck(c, m.MsgID)
		}
	case "admin":
		switch m.Type {
		case "send":
			text := []rune(m.Text)
			if len(text) == 0 {
				return
			}
			if len(text) > maxText {
				text = text[:maxText]
			}
			h.sendText(h.resolveTargets(m.Targets), string(text))
		case "clear":
			h.clearWatches(h.resolveTargets(m.Targets))
		case "settings":
			h.mu.Lock()
			a := m.Animate
			h.cfg.Animate = &a
			h.cfg.Reveal = m.Reveal
			h.saveCfg()
			h.logf("RÉGLAGES animation=%v révélation=%v", a, m.Reveal)
			h.pushState()
			h.mu.Unlock()
		case "rename":
			h.mu.Lock()
			if w, ok := h.watches[m.ID]; ok {
				name := []rune(m.Name)
				if len(name) > 40 {
					name = name[:40]
				}
				h.cfg.Names[strconv.Itoa(m.ID)] = string(name)
				w.Name = h.nameOf(m.ID)
				h.saveCfg()
				h.pushState()
			}
			h.mu.Unlock()
		}
	}
}

// ---------- Utilitaires ----------

func localIPs() []string {
	var out []string
	ifaces, _ := net.Interfaces()
	for _, i := range ifaces {
		if i.Flags&net.FlagUp == 0 || i.Flags&net.FlagLoopback != 0 {
			continue
		}
		addrs, _ := i.Addrs()
		for _, a := range addrs {
			if ipn, ok := a.(*net.IPNet); ok && ipn.IP.To4() != nil {
				out = append(out, ipn.IP.String())
			}
		}
	}
	return out
}

func openBrowser(url string) {
	var cmd *exec.Cmd
	switch runtime.GOOS {
	case "darwin":
		cmd = exec.Command("open", url)
	case "windows":
		cmd = exec.Command("rundll32", "url.dll,FileProtocolHandler", url)
	default:
		cmd = exec.Command("xdg-open", url)
	}
	_ = cmd.Start()
}

func dataDir() string {
	exe, err := os.Executable()
	if err != nil {
		return "."
	}
	d := filepath.Dir(exe)
	// go run : exécutable temporaire -> utiliser le dossier courant
	if filepath.Base(filepath.Dir(d)) == "go-build" || filepath.HasPrefix(d, os.TempDir()) {
		return "."
	}
	return d
}

// Annonce UDP en broadcast : les montres découvrent le laptop sans IP fixe.
// L'annonce part de l'adresse de chaque interface vers SON adresse de broadcast (ex. 192.168.0.255) :
// elle sort ainsi sur le bon réseau même quand un VPN capte la route par défaut.
func announce(port int) {
	msg := []byte(fmt.Sprintf("STAGEWATCH %d", port))
	for {
		ifaces, _ := net.Interfaces()
		for _, i := range ifaces {
			if i.Flags&net.FlagUp == 0 || i.Flags&net.FlagLoopback != 0 || i.Flags&net.FlagBroadcast == 0 {
				continue // les VPN (utun) n'ont pas de broadcast : ignorés
			}
			addrs, _ := i.Addrs()
			for _, a := range addrs {
				ipn, ok := a.(*net.IPNet)
				if !ok || ipn.IP.To4() == nil {
					continue
				}
				ip4, mask := ipn.IP.To4(), ipn.Mask[len(ipn.Mask)-4:]
				bcast := make(net.IP, 4)
				for k := 0; k < 4; k++ {
					bcast[k] = ip4[k] | ^mask[k]
				}
				c, err := net.DialUDP("udp4", &net.UDPAddr{IP: ip4}, &net.UDPAddr{IP: bcast, Port: 47808})
				if err != nil {
					continue
				}
				c.Write(msg)
				c.Close()
			}
		}
		time.Sleep(time.Second)
	}
}

func main() {
	log.SetFlags(0)
	port := flag.Int("port", 0, "port HTTP/WebSocket (par défaut : 47800, ou le suivant libre)")
	noBrowser := flag.Bool("no-browser", false, "ne pas ouvrir le navigateur")
	flag.Parse()

	h := newHub(dataDir())
	sub, _ := fs.Sub(webFS, "web")
	files := http.FileServer(http.FS(sub))

	mux := http.NewServeMux()
	mux.HandleFunc("/ws", h.serveWS)
	mux.HandleFunc("/api/photo", h.servePhotoAPI)
	mux.HandleFunc("/photo/", h.servePhoto)
	mux.HandleFunc("/api/info", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.Write(enc(map[string]any{"ips": localIPs(), "port": *port}))
	})
	// Pages et scripts : jamais de cache, pour que le navigateur prenne toujours la version de l'exécutable lancé.
	noCache := func(w http.ResponseWriter) { w.Header().Set("Cache-Control", "no-store") }
	page := func(name string) http.HandlerFunc {
		return func(w http.ResponseWriter, r *http.Request) {
			noCache(w)
			r.URL.Path = "/" + name + ".html"
			files.ServeHTTP(w, r)
		}
	}
	mux.HandleFunc("/admin", page("admin"))
	mux.HandleFunc("/public", page("public"))
	mux.HandleFunc("/sim", page("sim"))
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/" {
			http.Redirect(w, r, "/admin", http.StatusFound)
			return
		}
		noCache(w)
		files.ServeHTTP(w, r)
	})

	// Port : celui demandé, sinon 47800 ou le premier libre jusqu'à 47809.
	// On teste aussi 127.0.0.1 : une autre app peut n'écouter que là, et masquer StageWatch dans le navigateur.
	var ln net.Listener
	var err error
	tryPort := func(p int) (net.Listener, error) {
		if probe, e := net.Listen("tcp", fmt.Sprintf("127.0.0.1:%d", p)); e != nil {
			return nil, e
		} else {
			probe.Close()
		}
		return net.Listen("tcp", fmt.Sprintf(":%d", p))
	}
	if *port != 0 {
		ln, err = tryPort(*port)
	} else {
		for p := 47800; p < 47810; p++ {
			if ln, err = tryPort(p); err == nil {
				*port = p
				break
			}
		}
	}
	if err != nil {
		fmt.Printf("\nImpossible d'ouvrir le port %d (StageWatch est peut-être déjà lancé, ou le port est utilisé par une autre app)\n%v\n\nAppuie sur Entrée pour fermer.", *port, err)
		fmt.Scanln()
		os.Exit(1)
	}

	fmt.Println("==============================================")
	fmt.Println("  StageWatch est lancé")
	fmt.Println("==============================================")
	fmt.Printf("  Dirigeant : http://localhost:%d/admin\n", *port)
	fmt.Printf("  Public    : http://localhost:%d/public\n", *port)
	fmt.Printf("  Simulateur: http://localhost:%d/sim\n", *port)
	fmt.Println("  Adresses de ce laptop (les montres visent l'une d'elles) :")
	for _, ip := range localIPs() {
		fmt.Printf("     ws://%s:%d/ws\n", ip, *port)
	}
	fmt.Println("  Ferme cette fenêtre pour arrêter.")
	fmt.Println("==============================================")

	go announce(*port)

	if !*noBrowser {
		go func() {
			time.Sleep(400 * time.Millisecond)
			openBrowser(fmt.Sprintf("http://localhost:%d/admin", *port))
		}()
	}
	log.Fatal(http.Serve(ln, mux))
}
