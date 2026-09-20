# -*- coding: utf-8 -*-
# ============================================================================
#  logic/mailer.py  -  メール送信 (Python smtplib / ロリポップ SMTP)
# ----------------------------------------------------------------------------
#  smtp.lolipop.jp:465 (SSL/TLS)。config.local.py の "smtp" を使用。
#  password 未設定なら RuntimeError (呼び出し側で捕捉し「送信不可」を返す)。
# ============================================================================
import smtplib
from email.mime.text import MIMEText
from email.header import Header
from email.utils import formataddr
from .config import cfg


def smtp_ready():
    c = cfg().get("smtp") or {}
    return bool(c.get("host") and c.get("user") and c.get("password")
               and c.get("password") not in ("", "CHANGE_ME_mail_password", "YOUR_MAIL_PASSWORD"))


def send_mail(to_addr, subject, body):
    c = cfg().get("smtp") or {}
    if not smtp_ready():
        raise RuntimeError("smtp_not_configured")
    msg = MIMEText(body, "plain", "utf-8")
    msg["Subject"] = Header(subject, "utf-8")
    msg["From"] = formataddr((str(Header(c.get("from_name", "WetherLoggerBox"), "utf-8")), c["from"]))
    msg["To"] = to_addr
    port = int(c.get("port", 465))
    if c.get("ssl", True):
        with smtplib.SMTP_SSL(c["host"], port, timeout=15) as s:
            s.login(c["user"], c["password"])
            s.sendmail(c["from"], [to_addr], msg.as_string())
    else:
        with smtplib.SMTP(c["host"], port, timeout=15) as s:
            s.starttls()
            s.login(c["user"], c["password"])
            s.sendmail(c["from"], [to_addr], msg.as_string())
