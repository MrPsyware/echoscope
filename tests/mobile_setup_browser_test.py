"""Optional browser regression checks: pip install playwright; use system Chromium."""
from pathlib import Path
import argparse, json
from email.parser import BytesParser
from email.policy import default
from html import escape
from playwright.sync_api import sync_playwright
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--chromium');args=p.parse_args()
def fixture(mini,configured=True,saved=None):
    names=['types','registrations','calls'] if mini else ['watch_types','watch_regs','watch_calls']
    maximum=95 if mini else 255
    saved=saved or {}
    def field(label,name,value='',extra=''):
        value=escape(saved.get(name,value),quote=True)
        return f'<label>{label}<input name="{name}" value="{value}" {extra}></label>' if mini else f'<label>{label}</label><input name="{name}" value="{value}" {extra}>'
    return f'''<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><h1>EchoScope{' Mini' if mini else ''}</h1><p>Old intro</p><form action="/save" method="post" data-device="{'mini' if mini else 'original'}" data-configured="{int(configured)}"><input type=hidden name=token value=test-token><input type=hidden name=watch_ui value="{escape(saved.get('watch_ui',''),quote=True)}"><h2>Wi-Fi and location</h2>{field('Wi-Fi name','ssid','Home WiFi','required')}{field('Wi-Fi password','password','','type=password')}{field('Latitude','lat','51.5' if configured else '', 'type=number step=any min=-85 max=85 required')}{field('Longitude','lon','-0.1' if configured else '', 'type=number step=any min=-180 max=180 required')}<h2>Display</h2>{field('Brightness','brightness','70','type=number min=10 max=100 required')}<h2>Watchlist</h2><p>Old watch instructions</p>{field('Types',names[0],'A388',f'maxlength={maximum}')}{field('Registrations',names[1],'G-XLEA',f'maxlength={maximum}')}{field('Callsigns',names[2],'dr evil, EZY123',f'maxlength={maximum}')}<fieldset><legend>Advanced</legend>{field('Server URL','photo_url','http://server:8086')}</fieldset><button>Save</button></form><link rel=stylesheet href=/setup.css><script src=/setup.js defer></script>'''
with sync_playwright() as pw:
    browser=pw.chromium.launch(executable_path=args.chromium,headless=True,args=['--no-sandbox'])
    for mini in (False,True):
        page=browser.new_page(viewport={'width':390,'height':844},device_scale_factor=1)
        page.set_default_timeout(10000)
        errors=[];requests=[];saved={}
        def page_error(e):
            errors.append(str(e));print('Page error:',e,flush=True)
        page.on('pageerror',page_error)
        def serve(route):
            url=route.request.url
            if url.endswith('/setup.css'):route.fulfill(body=(ROOT/'web/setup.css').read_text(),content_type='text/css')
            elif url.endswith('/setup.js'):route.fulfill(body=(ROOT/'web/setup.js').read_text(),content_type='application/javascript')
            elif route.request.method=='POST':
                requests.append((url,route.request.post_data))
                if url.endswith('/watchlist'):
                    content_type=route.request.headers['content-type']
                    message=BytesParser(policy=default).parsebytes(('Content-Type: '+content_type+'\r\n\r\n').encode()+route.request.post_data_buffer)
                    for part in message.iter_parts():saved[part.get_param('name',header='content-disposition')]=part.get_payload(decode=True).decode()
                route.fulfill(body='Saved',content_type='text/plain')
            elif 'Offline' in url:route.abort()
            elif 'geocoding-api' in url:route.fulfill(json={'results':[{'name':'Crawley','country':'United Kingdom','latitude':51.113,'longitude':-0.183}]})
            elif 'postcodes.io' in url:route.fulfill(json={'result':{'postcode':'RH6 0NP','country':'England','latitude':51.15,'longitude':-0.19}})
            elif 'openstreetmap.org' in url:route.fulfill(body='<p>Map fixture</p>',content_type='text/html')
            else:route.fulfill(body=fixture(mini,'first' not in url,saved),content_type='text/html')
        page.route('**/*',serve);page.goto('http://device.test/')
        page.get_by_role('heading',name='Your watchlist').wait_for()
        assert page.locator('article.card').count()==4
        page.get_by_role('button',name='Add a watch',exact=True).click()
        page.get_by_label('Aircraft type',exact=True).select_option('B744')
        page.get_by_label('Friendly name (optional)').fill('Dad <Jumbo>')
        page.get_by_role('button',name='Add watch',exact=True).click()
        assert page.locator('article.card').count()==5
        assert page.get_by_role('heading',name='Dad <Jumbo>').count()==1
        page.locator('article.card').last.get_by_label('Enabled').uncheck()
        page.get_by_role('button',name='Save watchlist',exact=True).click()
        page.get_by_text('Watchlist saved. Changes are active—no restart needed.',exact=True).wait_for()
        assert requests[-1][0].endswith('/watchlist') and 'name="ssid"' not in requests[-1][1]
        assert page.locator('input[name=ssid]').input_value()=='Home WiFi'
        page.evaluate('scrollTo(0,0)')
        page.screenshot(path=f'/tmp/echoscope-mobile-{"mini" if mini else "original"}-watch.png',full_page=True)
        page.reload();page.get_by_role('heading',name='Dad <Jumbo>').wait_for()
        assert not page.locator('article.card').last.get_by_label('Enabled').is_checked()
        page.locator('article.card').last.get_by_role('button',name='Edit').click();page.get_by_role('button',name='Delete watch').click()
        assert page.locator('article.card').count()==4
        page.get_by_role('button',name='Settings',exact=True).click()
        page.get_by_label('Town or postcode').fill('Crawley, UK');page.get_by_role('button',name='Find location').click()
        page.get_by_role('button',name='Crawley, United Kingdom').click()
        assert page.locator('input[name=lat]').input_value()=='51.113000'
        page.get_by_label('Town or postcode').fill('RH6 0NP');page.get_by_role('button',name='Find location').click()
        page.get_by_role('button',name='RH6 0NP, England').click()
        assert page.locator('input[name=lon]').input_value()=='-0.190000'
        page.get_by_label('Town or postcode').fill('Offline');page.get_by_role('button',name='Find location').click()
        page.get_by_text('Search unavailable.',exact=False).wait_for()
        assert page.locator('input[name=lon]').input_value()=='-0.190000'
        page.get_by_role('button',name='Show confirmation map').click();assert page.locator('iframe').count()==1
        page.get_by_role('button',name='Save settings',exact=True).click()
        page.wait_for_function("document.querySelector('.status').textContent.includes('Settings saved')")
        assert requests[-1][0].endswith('/save') and 'http://server:8086' in requests[-1][1]
        page.goto('http://device.test/first');page.get_by_role('button',name='Connect Wi-Fi first').click()
        page.wait_for_function("document.querySelector('.status').textContent.includes('Wi-Fi saved')")
        assert requests[-1][0].endswith('/network') # Coordinates intentionally empty.
        page.evaluate('scrollTo(0,0)')
        page.screenshot(path=f'/tmp/echoscope-mobile-{"mini" if mini else "original"}-setup.png',full_page=True)
        assert not errors,errors
        assert page.evaluate('document.documentElement.scrollWidth <= innerWidth')
        page.close()
    browser.close()
print('Both mobile variants: watch editor, isolated save, location lookup/map, settings preservation, Wi-Fi-first flow and mobile layout passed.')
