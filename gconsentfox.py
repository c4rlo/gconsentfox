#!/usr/bin/python

import configparser
import datetime
import os
import pathlib
import sqlite3
import sys


FIREFOX_BIN = "/usr/bin/firefox"

COOKIES = (  # name, value, host
    ("ANID", "OPT_OUT", ".google.com"),
    ("ANID", "OPT_OUT", ".google.co.uk"),
    ("CONSENT", "YES+cb", ".google.com"),
    ("CONSENT", "YES+cb", ".google.co.uk"),
    ("CONSENT", "YES+cb", ".youtube.com"),
)

SQL = """\
INSERT OR IGNORE INTO moz_cookies
    (originAttributes, name, value, host, path, expiry, lastAccessed, creationTime,
     isSecure, isHttpOnly, inBrowserElement, sameSite, rawSameSite, schemeMap)
  VALUES ('', ?, ?, ?, '/', ?, ?, ?, 1, 1, 0, 0, 0, 2)\
"""


def get_cookies_filename():
    firefox_path = pathlib.Path.home() / ".mozilla/firefox"
    profiles = configparser.ConfigParser()
    profiles.read(firefox_path / "profiles.ini")
    for section in profiles.sections():
        if section.startswith("Install"):
            profile = profiles[section]["Default"]
            return firefox_path / profile / "cookies.sqlite"


def main():
    print("Inserting Google cookie consent cookies")
    db = sqlite3.connect(get_cookies_filename(), timeout=0.25)
    cur = db.cursor()
    expiry_ts = datetime.datetime(2100, 1, 1, tzinfo=datetime.UTC).timestamp()
    now_tsms = datetime.datetime.now(tz=datetime.UTC).timestamp() * 1000000
    try:
        for cookie in COOKIES:
            cur.execute(SQL, (*cookie, expiry_ts, now_tsms, now_tsms))
    except sqlite3.OperationalError as e:
        print(f"Failed to add cookies: {e}. Firefox already running?")
    db.commit()
    db.close()
    print("Starting Firefox")
    os.execl(FIREFOX_BIN, FIREFOX_BIN, *sys.argv[1:])


main()
