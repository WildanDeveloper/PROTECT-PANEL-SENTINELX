CXX      = g++
CXXFLAGS = -O2 -std=c++17 -Wall
LDFLAGS  = -lcurl -lpthread -lssl -lcrypto

TARGET   = sentinelx

SRCS = main.cpp config.cpp http.cpp telegram.cpp api.cpp \
       disk_protect.cpp rate_protect.cpp integrity.cpp \
       selfguard.cpp tracking.cpp logger.cpp bot.cpp db_guard.cpp

OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "  ✅ Build selesai: ./$(TARGET)"
	@echo ""

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

install: $(TARGET)
	@echo "  >> Meng-install SentinelX..."
	cp $(TARGET) /usr/local/bin/$(TARGET)
	chmod 755 /usr/local/bin/$(TARGET)
	@echo "  >> Membuat systemd service..."
	@echo "[Unit]"                                              > /etc/systemd/system/sentinelx.service
	@echo "Description=SentinelX Pterodactyl Protection Daemon" >> /etc/systemd/system/sentinelx.service
	@echo "After=network.target"                               >> /etc/systemd/system/sentinelx.service
	@echo ""                                                   >> /etc/systemd/system/sentinelx.service
	@echo "[Service]"                                          >> /etc/systemd/system/sentinelx.service
	@echo "Type=simple"                                        >> /etc/systemd/system/sentinelx.service
	@echo "ExecStart=/usr/local/bin/sentinelx"                 >> /etc/systemd/system/sentinelx.service
	@echo "Restart=always"                                     >> /etc/systemd/system/sentinelx.service
	@echo "RestartSec=3"                                       >> /etc/systemd/system/sentinelx.service
	@echo "User=root"                                          >> /etc/systemd/system/sentinelx.service
	@echo ""                                                   >> /etc/systemd/system/sentinelx.service
	@echo "[Install]"                                          >> /etc/systemd/system/sentinelx.service
	@echo "WantedBy=multi-user.target"                         >> /etc/systemd/system/sentinelx.service
	systemctl daemon-reload
	systemctl enable --now sentinelx
	@echo "  ✅ SentinelX terinstall dan berjalan!"

uninstall:
	systemctl stop sentinelx || true
	systemctl disable sentinelx || true
	chattr -i /usr/local/bin/sentinelx 2>/dev/null || true
	rm -f /usr/local/bin/sentinelx
	rm -f /etc/systemd/system/sentinelx.service
	rm -f /etc/sentinelx.conf
	rm -f /var/run/sentinelx.pid
	systemctl daemon-reload
	@echo "  SentinelX diuninstall."

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all install uninstall clean
