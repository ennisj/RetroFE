# Generates the fork's Aeon Nox pages from upstream's layout.xml / "layout 4x3.xml".
#   python gen_layouts.py <upstream Aeon Nox folder> <output Aeon Nox folder>
# See README.md next to this script.
#
# Upstream draws systems (menu 0) and games (menu 1) on one page. This fork builds a
# page per collection, so every list is menu 0; the two views become separate pages:
#   layout.xml                           games view, used by every collection of games
#   collections/_menu/layout/layout.xml  systems view, for menus of collections (Browse)
#   collections/Home/layout/layout.xml   Home, in the systems style
#   collections/Catalog/layout/...       the Catalog list, in the systems style
# plus 4x3 twins of each, splash.xml and the form/keyboard pages.
import os, re, sys
import xml.etree.ElementTree as ET

SRC, OUT = sys.argv[1], sys.argv[2]
ACCENT = '75c8f7'

def read(name):
    with open(os.path.join(SRC, name), encoding='utf-8') as f:
        return f.read().replace('\r\n', '\n')

def write(rel, text):
    path = os.path.join(OUT, *rel.split('/'))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    text = text.strip('\n') + '\n'
    if rel.endswith('.xml'):
        try:
            ET.fromstring(text)
        except ET.ParseError as e:
            lines = text.split('\n')
            ln = e.position[0]
            raise SystemExit('%s: %s\n%s' % (rel, e, '\n'.join(lines[max(0, ln - 3):ln + 2])))
    with open(path, 'w', encoding='utf-8', newline='\r\n') as f:
        f.write(text)
    print('wrote', rel)

# ---------------------------------------------------------------------------------------
# Upstream blocks

def blocks_of(text):
    """Top-level components (4-space indent) with the comments before them."""
    lines = text.split('\n')
    out, pending, i = [], [], 0
    while i < len(lines):
        ln = lines[i]
        if re.match(r'^    <(?!/|!--)\w', ln):
            j = i
            if not ln.rstrip().endswith('/>'):
                while not re.match(r'^    </', lines[j]):
                    j += 1
            out.append({'pre': pending, 'text': '\n'.join(lines[i:j + 1])})
            pending, i = [], j + 1
            continue
        if ln.startswith('    <!--') or (ln.strip() == '' and pending):
            pending.append(ln)
        i += 1
    return out

def kind(b):
    t = b['text']
    sys0 = 'menuIndex="0"' in t
    games = 'menuIndex="!0"' in t or 'menuIndex="i"' in t
    if sys0 and games:
        return 'both'
    return 'systems' if sys0 else 'games' if games else 'shared'

def tidy(t):
    t = re.sub(r'\s+menuIndex="!?0"', '', t)
    # Closing tags that don't match their opening tag (rapidxml doesn't mind; XML does)
    t = re.sub(r'<(on\w+)((?:\s[^>]*)?)>(.*)</on\w+>', r'<\1\2>\3</\1>', t)
    t = t.replace('</relodableVideo>', '</reloadableVideo>')
    # Re-align "<onX>   <set" columns after removing menuIndex
    t = re.sub(r'^(\s*)(<on\w+>)[ ]*(<set)', lambda m: m.group(1) + m.group(2).ljust(19) + m.group(3), t, flags=re.M)
    return t

def find(blocks, needle):
    for b in blocks:
        if needle in b['text']:
            return b
    raise KeyError(needle)

def drop(blocks, *needles):
    return [b for b in blocks if not any(n in b['text'] for n in needles)]

def sub(b, old, new, count=1):
    if old not in b['text']:
        raise KeyError(old)
    b['text'] = b['text'].replace(old, new, count)

def render(blocks):
    parts = []
    for b in blocks:
        pre = [re.sub(r'<!--(-{3,})-->', lambda m: '<!-- ' + '=' * (len(m.group(1)) - 2) + ' -->', p) for p in b['pre']]
        parts.append('\n'.join(pre + [tidy(b['text'])]))
    return '\n\n\n'.join(p.strip('\n') for p in parts)

def page(header, body, note, up=''):
    # Image and sound paths are relative to the page's folder. RetroFE falls back to the
    # layout's folder, but logs an error first, so collection pages point there directly.
    if up:
        body = body.replace('src="images/', 'src="%simages/' % up).replace('src="sounds/', 'src="%ssounds/' % up)
    return '%s\n\n<!-- %s -->\n\n%s\n\n\n</layout>\n' % (header, note, body)

# Tween sets reused by new components
SHOW = '''        <onEnter>          <set duration=".1"><animate type="alpha" to="{a}"/></set></onEnter>
        <onExit>           <set duration=".1"><animate type="alpha" to="0"/></set></onExit>
        <onMenuEnter>      <set duration=".1"><animate type="alpha" to="{a}"/></set></onMenuEnter>
        <onMenuExit>       <set duration=".1"><animate type="alpha" to="0"/></set></onMenuExit>
        <onGameEnter>      <set duration=".1"><animate type="alpha" to="0"/></set></onGameEnter>
        <onGameExit>       <set duration=".1"><animate type="alpha" to="{a}"/></set></onGameExit>'''
SHOW_ITEM = SHOW + '''
        <onHighlightEnter> <set duration=".1"><animate type="alpha" to="{a}"/></set></onHighlightEnter>
        <onHighlightExit>  <set duration=".1"><animate type="alpha" to="0"/></set></onHighlightExit>'''

def comp(tag, attrs, item=True, a='1'):
    tweens = (SHOW_ITEM if item else SHOW).format(a=a)
    return {'pre': [], 'text': '    <%s %s>\n%s\n    </%s>' % (tag, attrs, tweens, tag)}

def comment(text):
    return '    <!-- %s -->' % text

def with_pre(b, *lines):
    b['pre'] = list(lines)
    return b

def background(W, H):
    return {'pre': [comment('Background (the fork adds it: upstream is black where there is no fanart)')],
            'text': '    <image src="images/background.png" x="0" y="0" width="%d" height="%d" layer="0"/>' % (W, H)}

# ---------------------------------------------------------------------------------------
# Geometry: upstream's 16:9 and 4:3 positions, plus the fork's new components

GEO = {
    '': dict(W=1920, H=1080, file='layout.xml',
             box_x=180, title_y=220, line2_y=280, line3_y=320, tagline_y=250, box_w=1120,
             right_x=1555, right_y=220, right_w=360, right_h=260,
             bar2_y=623, bar2_x=110, bar2_w=1700,
             dev_x=480, dev_y=855, lab_x=750, val_x=1000, val_w=650, rows=(785, 855, 925),
             desc_x=300, desc_y=750, desc_w=1320, desc_h=300, sdesc_x=750, sdesc_w=880,
             logo_font=48, logo_w=460, box_font=40),
    ' 4x3': dict(W=1600, H=1200, file='layout 4x3.xml',
             box_x=140, title_y=220, line2_y=280, line3_y=320, tagline_y=250, box_w=980,
             right_x=1300, right_y=220, right_w=320, right_h=240,
             bar2_y=683, bar2_x=80, bar2_w=1440,
             dev_x=350, dev_y=975, lab_x=750, val_x=980, val_w=520, rows=(905, 975, 1045),
             desc_x=180, desc_y=870, desc_w=1240, desc_h=300, sdesc_x=750, sdesc_w=760,
             logo_font=48, logo_w=470, box_font=40),
}

# ---------------------------------------------------------------------------------------

def split(variant):
    g = GEO[variant]
    text = read(g['file'])
    header = text.split('\n', 1)[0]
    blocks = blocks_of(text)
    shared = [b for b in blocks if kind(b) == 'shared']
    systems = [b for b in blocks if kind(b) == 'systems']
    games = [b for b in blocks if kind(b) == 'games']
    both = [b for b in blocks if kind(b) == 'both']
    return g, header, shared, systems, games, both

def main_video(g, systems_view):
    """Upstream's one video moves between the systems and games spots; each page now has one spot."""
    if systems_view:
        x, y, w, h = g['right_x'], g['right_y'], (400 if g['W'] == 1920 else 360), (300 if g['W'] == 1920 else 280)
    else:
        x, y, w, h = (1450, 350, 594, 445) if g['W'] == 1920 else (1220, 320, 480, 360)
    return {'pre': [comment('Main video')],
            'text': ('    <reloadableVideo imageType="screenshot" alpha="1" x="%d" y="%d" width="%d" maxHeight="%d" xOrigin="center" yOrigin="center" layer="9">\n' % (x, y, w, h)) +
            (SHOW_ITEM + '\n        <onMenuJumpEnter>  <set duration=".1"><animate type="alpha" to="1"/></set></onMenuJumpEnter>\n'
             '        <onMenuJumpExit>   <set duration=".1"><animate type="alpha" to="0"/></set></onMenuJumpExit>\n'
             '        <onPlaylistEnter>  <set duration=".1"><animate type="alpha" to="1"/></set></onPlaylistEnter>\n'
             '        <onPlaylistExit>   <set duration=".1"><animate type="alpha" to="0"/></set></onPlaylistExit>').format(a='1') +
            '\n    </reloadableVideo>'}

def playlist(g, both, systems_view):
    b = dict(find(both, 'type="playlist"'))
    t = b['text']
    # Keep the half of upstream's tweens that applies to this page
    t = re.sub(r'\n        <onMenuEnter menuIndex="%s">.*?</onMenuEnter>' % ('!0' if systems_view else '0'), '', t, flags=re.S)
    t = re.sub(r'\n        <onMenuExit menuIndex="%s">.*?</onMenuExit>' % ('!0' if systems_view else '0'), '', t, flags=re.S)
    b['text'] = t
    b['pre'] = [comment('Playlist')]
    return b

def sounds_fanart(shared, fanart=True):
    out = []
    for b in shared:
        if 'type="fanart"' in b['text'] and not fanart:
            continue
        out.append(dict(b))
    return out

# ---------------------------------------------------------------------------------------
# Games view: the root layout.xml

def add_on_enter(b):
    """Upstream shows the games view on onMenuEnter (menu 1 of the systems page). Here
    it's a page of its own, which starts with onEnter, so give it the same tweens."""
    t = b['text']
    if '<onEnter' in t or '<onMenuEnter' not in t:
        return
    m = re.search(r'^([ ]*)<onMenuEnter(?:\s+menuIndex="!0")?\s*>(.*?)</onMenuEnter>', t, flags=re.S | re.M)
    enter = '%s<onEnter>%s</onEnter>\n' % (m.group(1), m.group(2))
    b['text'] = t[:m.start()] + enter + t[m.start():]

def games_page(variant):
    g, header, shared, systems, games, both = split(variant)
    blocks = sounds_fanart(shared)
    blocks.insert(4, background(g['W'], g['H']))
    games = [dict(b) for b in games]
    for b in games:
        if '<menu ' not in b['text']:
            add_on_enter(b)

    # The vertical story used fonts/MC360.ttf (Ubuntu Condensed under the Ubuntu Font Licence,
    # shipped without it); Roboto Condensed is close, already here, and covers more scripts
    story = find(games, 'type="story"')
    sub(story, 'fonts/MC360.ttf', 'fonts/RobotoCondensed-Regular.ttf')
    sub(story, ' alpha="1" layer="19"', ' layer="19"')   # alpha was given twice

    # Top bar: name the collection in text. Upstream shows its system logo, which most
    # collections made in Manage don't have.
    logo = find(games, 'mode="system" type="logo"')
    m = re.search(r'x="(\d+)"', logo['text'])
    logo['text'] = re.sub(r'<reloadableImage mode="system" type="logo" x="\d+" y="-68" maxWidth="\d+" height="45" xOrigin="left"',
                          '<reloadableText type="collectionName" x="%s" y="-68" maxWidth="%d" xOrigin="left"' % (m.group(1), 640 if g['W'] == 1920 else 520),
                          logo['text']).replace('</reloadableImage>', '</reloadableText>')
    logo['text'] = logo['text'].replace('layer="10">', 'fontSize="42" loadFontSize="42" layer="10">', 1)
    logo['text'] = logo['text'].replace('to="23"', 'to="25"').replace('from="23"', 'from="25"')
    logo['pre'] = [comment('Collection name')]

    # "ITEMS" and the count were drawn on top of each other; draw them as one text
    items = find(games, 'value="ITEMS"')
    games.remove(items)
    count = find(games, 'type="collectionIndexSize"')
    ix = re.search(r'value="ITEMS" x="(\d+)"', items['text']).group(1)
    count['text'] = re.sub(r'type="collectionIndexSize" menuScrollReload="true" x="\d+"',
                           'type="collectionIndexSize" singlePrefix="ITEMS  " pluralPrefix="ITEMS  " menuScrollReload="true" x="%s"' % ix, count['text'])
    count['pre'] = [comment('"ITEMS" and the position in the list')]

    # The wall's labels only show for games that have the value: each label is the
    # value's prefix, padded with spaces and cut off by maxWidth
    labels = {'Publisher:': 'manufacturer', 'Developer:': 'developer', 'Genre:': 'genre',
              'Rating:': 'rating', 'Year:': 'year', 'Score:': 'score'}
    for label, type_ in labels.items():
        b = find(games, 'value="%s"' % label)
        pad = label + ' ' * 60
        b['text'] = b['text'].replace('<text value="%s"' % label,
                                      '<reloadableText type="%s" singlePrefix="%s" pluralPrefix="%s" maxWidth="300"' % (type_, pad, pad), 1)
        b['text'] = b['text'].replace('</text>', '</reloadableText>')
    find(games, 'singlePrefix="Publisher:')['pre'] = [
        comment('Labels on the wall, shown only when the game has a value (the label is the value\'s prefix, cut off by maxWidth)')]

    menu = find(games, 'imageType="artwork_front"')
    menu['text'] = menu['text'].replace('miScrollTime', 'minScrollTime')
    # Games without box art show their title: a smaller, condensed font fits the box
    menu['text'] = re.sub(r'(<itemDefaults [^>]*?)loadFontSize="50" fontSize="50"', r'\1font="fonts/Oswald-Regular.ttf" loadFontSize="%d" fontSize="%d"' % (g['box_font'], g['box_font']), menu['text'])
    menu['text'] = re.sub(r'(<itemDefaults [^>]*?)fontSize="50" loadFontSize="50"', r'\1font="fonts/Oswald-Regular.ttf" fontSize="%d" loadFontSize="%d"' % (g['box_font'], g['box_font']), menu['text'])
    menu['text'] = re.sub(r'(<menu [^>]*?) font="fonts/Oswald-Regular.ttf" fontSize="20" loadFontSize="20"', r'\1', menu['text'])

    blocks += games + [main_video(g, False), playlist(g, both, False)]
    return page(header, render(blocks), 'Aeon Nox: a collection\'s games. Every collection without a page of its own uses this one.')

# ---------------------------------------------------------------------------------------
# Systems view: Main, and the base for Home and the Catalog

def systems_blocks(variant):
    g, header, shared, systems, games, both = split(variant)
    systems = [dict(b) for b in systems]
    if variant == ' 4x3':
        # Upstream's 4:3 date ends where the time starts
        sub(find(systems, 'timeFormat="%A, %B %d, %Y"'), 'x="1360"', 'x="1340"')
    menu = find(systems, 'imageType="logo"')
    # Collections without a logo show their name, in the theme's display font
    sub(menu, 'orientation="horizontal"', 'orientation="horizontal" textFallback="true"')
    menu['text'] = re.sub(r'(<itemDefaults [^>]*?)(loadFontSize="\d+" fontSize="\d+"|fontSize="\d+" loadFontSize="\d+")',
                          r'\1font="fonts/BebasNeue.ttf" fontColor="ffffff" loadFontSize="%d" fontSize="%d"' % (g['logo_font'], g['logo_font']), menu['text'])
    # Text stops at maxWidth, mid-word: leave room for long names (items are 480 or 500 apart)
    sub(menu, '<itemDefaults alpha="0.2" x="center" y="center" maxWidth="400"', '<itemDefaults alpha="0.2" x="center" y="center" maxWidth="%d"' % g['logo_w'])
    # The video's black backing showed as an empty black box for systems without a video
    systems = drop(systems, 'images/black.png')
    return g, header, shared, systems, both

def menu_page(variant):
    g, header, shared, systems, both = systems_blocks(variant)
    blocks = sounds_fanart(shared)
    blocks.insert(4, background(g['W'], g['H']))
    placeholder = {'pre': [comment('Under the video (layer 9), so it only shows for systems without one')],
                   'text': ('    <image src="../../Home/medium_artwork/icon/Browse.png" x="%d" y="%d" maxWidth="%d" maxHeight="%d" alpha="0" xOrigin="center" yOrigin="center" layer="8">\n'
                            % (g['right_x'], g['right_y'], g['right_w'], g['right_h'])) + SHOW.format(a='0.35') + '\n    </image>'}
    blocks += systems + [placeholder, main_video(g, True), playlist(g, both, True)]
    return page(header, render(blocks), 'Aeon Nox: a menu of collections, such as Main (Browse): one logo per collection. '
                'RetroFE uses collections/_menu/layout for any collection whose items are all collections, '
                'unless it has a page of its own.', up='../../../')

def hint(g, text):
    return with_pre(comp('text', 'value="%s" alpha="0" x="%d" y="%d" xOrigin="left" yOrigin="center" fontSize="34" loadFontSize="34" fontColor="dedede" layer="10"' % (text, g['bar2_x'], g['bar2_y']), item=False, a='0.75'),
                    comment('Controls'))

def home_page(variant):
    g, header, shared, systems, both = systems_blocks(variant)
    blocks = sounds_fanart(shared, fanart=False)
    blocks.insert(4, background(g['W'], g['H']))
    # Home's items and their artwork are built in, so the logos come from the layout:
    # collections/Home/medium_artwork/logo/<item>.png
    menu = find(systems, 'imageType="logo"')
    sub(menu, '<menu type="custom" imageType="logo"', '<menu type="custom" mode="layout" imageType="logo"')
    # Only four items: show the selected one and its neighbours, so none appears twice
    menu['text'] = re.sub(r'<item xOffset="(-?\d+)">.*?</item>',
                          lambda m: m.group(0) if abs(int(m.group(1))) < 900 else '<item xOffset="%s" alpha="0"/>' % m.group(1),
                          menu['text'], flags=re.S)
    keep = drop(systems, 'reloadableVideo', 'type="manufacturer"', 'type="year"', 'type="story"', 'type="lb_', 'type="type"',
                'type="generation"', 'type="media"', 'type="cpu"', 'type="device"', 'images/black.png')
    title = find(keep, 'type="title"')
    sub(title, 'y="%d"' % g['title_y'], 'y="%d"' % (g['title_y'] - 30))
    sub(title, 'maxWidth="900"', 'maxWidth="%d"' % g['box_w'])
    W = g['W']
    new = [
        with_pre(comp('reloadableScrollingText', 'type="tagline" mode="layout" alpha="0" x="%d" y="%d" width="%d" height="64" xOrigin="left" yOrigin="top" font="fonts/Oswald-Regular.ttf" fontSize="46" loadFontSize="46" fontColor="ffffff" direction="vertical" startPosition="0" layer="10"'
                      % (g['box_x'], g['tagline_y'], g['box_w'])),
                 comment('What each item is for: collections/Home/medium_artwork/tagline/<item>.txt')),
        with_pre(comp('reloadableImage', 'type="icon" mode="layout" alpha="0" x="%d" y="%d" maxWidth="%d" maxHeight="%d" xOrigin="center" yOrigin="center" layer="10"'
                      % (g['right_x'], g['right_y'], g['right_w'], g['right_h'])),
                 comment('Icon: collections/Home/medium_artwork/icon/<item>.png')),
        with_pre(comp('reloadableScrollingText', 'type="description" mode="layout" alpha="0" x="%d" y="%d" width="%d" height="%d" xOrigin="left" yOrigin="top" font="fonts/OpenSans.ttf" fontSize="40" loadFontSize="40" fontColor="f2f2f2" direction="vertical" startPosition="0" layer="10"'
                      % (g['desc_x'], g['desc_y'], g['desc_w'], g['desc_h'])),
                 comment('More about the item: collections/Home/medium_artwork/description/<item>.txt')),
        hint(g, 'Left/Right: choose     Select: open'),
    ]
    return page(header, render(blocks + keep + new), 'Aeon Nox: Home (Catalog, Settings, Manage, Browse)', up='../../../')

def label_value(g, row, label, type_, fmt=''):
    """An accent label and a white value that both disappear when the item has no value.
    The label is the value's prefix padded with spaces and cut off by maxWidth."""
    y = g['rows'][row]
    pad = label + ' ' * 60
    lab = comp('reloadableText', 'type="%s" singlePrefix="%s" pluralPrefix="%s" alpha="0" x="%d" y="%d" maxWidth="230" height="50" xOrigin="left" yOrigin="center" font="fonts/BebasNeue.ttf" fontSize="50" loadFontSize="50" fontColor="%s" layer="10"'
               % (type_, pad, pad, g['lab_x'], y, ACCENT))
    val = comp('reloadableText', 'type="%s"%s alpha="0" x="%d" y="%d" maxWidth="%d" height="50" xOrigin="left" yOrigin="center" font="fonts/BebasNeue.ttf" fontSize="50" loadFontSize="50" fontColor="ffffff" layer="10"'
               % (type_, fmt, g['val_x'], y, g['val_w']))
    return [lab, val]

def catalog_page(variant):
    g, header, shared, systems, both = systems_blocks(variant)
    blocks = sounds_fanart(shared, fanart=False)
    blocks.insert(4, background(g['W'], g['H']))
    keep = drop(systems, 'type="story"', 'type="lb_', 'type="type"', 'type="generation"', 'type="media"', 'type="cpu"',
                'type="device"', 'type="manufacturer"', 'type="year"')
    keep.append(main_video(g, True))
    # Package names are often longer than system names: use the whole box
    sub(find(keep, 'type="title"'), 'maxWidth="900"', 'maxWidth="%d"' % g['box_w'])
    new = [
        with_pre(comp('reloadableScrollingText', 'type="tagline" mode="layout" alpha="0" x="%d" y="%d" width="%d" height="64" xOrigin="left" yOrigin="top" font="fonts/Oswald-Regular.ttf" fontSize="46" loadFontSize="46" fontColor="ffffff" direction="vertical" startPosition="0" layer="10"'
                      % (g['box_x'], g['tagline_y'], g['box_w'])),
                 comment('A line under the title for the Sources item: collections/Catalog/medium_artwork/tagline/Catalog Sources.txt')),
        with_pre(comp('reloadableImage', 'type="logo" alpha="0" x="%d" y="%d" maxWidth="440" maxHeight="280" xOrigin="center" yOrigin="center" layer="11"'
                      % (g['dev_x'], g['dev_y'])),
                 comment('The package\'s logo, large, where systems show their device')),
        with_pre(comp('reloadableImage', 'type="icon" mode="layout" alpha="0" x="%d" y="%d" maxWidth="360" maxHeight="260" xOrigin="center" yOrigin="center" layer="11"'
                      % (g['dev_x'], g['dev_y'])),
                 comment('Sources has no logo; it gets an icon from the layout: collections/Catalog/medium_artwork/icon/Catalog Sources.png')),
    ]
    lv = label_value(g, 0, 'KIND', 'genre') + label_value(g, 1, 'SOURCE', 'manufacturer') + label_value(g, 2, 'VERSION', 'year')
    lv[0]['pre'] = [comment('Kind, source and version. Each label is its value\'s prefix, padded and cut off by maxWidth, so it only shows when there is a value.')]
    new += lv
    new += [
        with_pre(comp('reloadableScrollingText', 'type="description" mode="layout" alpha="0" x="%d" y="%d" width="%d" height="%d" xOrigin="left" yOrigin="top" font="fonts/OpenSans.ttf" fontSize="38" loadFontSize="38" fontColor="f2f2f2" direction="vertical" startPosition="0" layer="10"'
                      % (g['sdesc_x'], g['desc_y'] + 8, g['sdesc_w'], g['desc_h'] - 30)),
                 comment('Text for the Sources item: collections/Catalog/medium_artwork/description/Catalog Sources.txt')),
        hint(g, 'Left/Right: choose     Select: details     Back: Home'),
    ]
    return page(header, render(blocks + keep + new), 'Aeon Nox: the Catalog (one item per package, then Sources)', up='../../../')

# ---------------------------------------------------------------------------------------
# Form and keyboard pages. RetroFE uses forms/<screen>/<page>.xml, then forms/<page>.xml,
# for the page collections are drawn with ("layout" or, with randomLayout, "layout 4x3").

def form_attrs(up):
    # focusImage is relative to the page's own folder. Details (a result's, a link's
    # status) in a pale accent, section headers and separators in the accent, pictures
    # on a faint light backing, results in green, messages that report nothing happened
    # (cancelled, undone) in a quiet blue-grey, errors and a button waiting for
    # confirmation in red.
    lines = [
        'font="fonts/OpenSans.ttf" loadFontSize="40" fontSize="34" fontColor="dedede" focusColor="ffffff"',
        'rowHeight="50" focusImage="%simages/form_focus.png" noteAlpha="0.6" disabledAlpha="0.35"' % up,
        'rowDetailColor="a9d8f5" rowDetailAlpha="0.8" headerColor="%s" headerAlpha="0.95"' % ACCENT,
        'separatorColor="%s" separatorAlpha="0.18" imageBackgroundColor="ffffff" imageBackgroundAlpha="0.06"' % ACCENT,
        'statusColor="9fe3a6" infoColor="b8c4d0" errorColor="ff9a8a" confirmBackgroundColor="d9442f" confirmBackgroundAlpha="0.3"',
        'backgroundColor="000000" backgroundAlpha="0"',
    ]
    return ('\n' + ' ' * 10).join(lines)

# The info panel images have a transparent margin (12 px of 811 x 214) that grows when
# they're stretched, so they're drawn larger than the panel that shows.
def panel_size(w, h):
    return round(w / (1 - 24 / 811)), round(h / (1 - 24 / 214))

FORMS = {
    '': dict(W=1920, H=1080, file='layout.xml', panel=(232, 70, 1688, 1040), form_w=1400, label=460,
             pkg_cover=655, pkg_panel=(184, 70, 1736, 640), pkg_form_w=1500, pkg_label=420,
             kb=(1440, 588, 1050)),
    ' 4x3': dict(W=1600, H=1200, file='layout 4x3.xml', panel=(100, 70, 1500, 1160), form_w=1320, label=420,
                 pkg_cover=775, pkg_panel=(60, 70, 1540, 755), pkg_form_w=1420, pkg_label=400,
                 kb=(1400, 640, 1170)),
}

def head(f):
    return '<layout width="%d" height="%d" font="fonts/OpenSans.ttf" loadFontSize="40" fontColor="dedede">' % (f['W'], f['H'])

def topbar(f, label):
    return ('''    <!-- Top bar (no clock: a form page has no selected item, which reloadableText needs) -->
    <image src="images/topbar.png" x="0" y="0" width="%d" height="50" layer="1"/>
    <text value="%s" x="24" y="25" xOrigin="left" yOrigin="center" font="fonts/BebasNeue.ttf" fontSize="46" loadFontSize="46" fontColor="%s" layer="2"/>'''
            % (f['W'], label, ACCENT))

def panel_images(x0, y0, x1, y1):
    w, h = panel_size(x1 - x0, y1 - y0)
    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
    return ('''    <image src="images/system_info_bg.png" x="%d" y="%d" width="%d" height="%d" xOrigin="center" yOrigin="center" layer="2"/>
    <image src="images/system_info_overlay.png" x="%d" y="%d" width="%d" height="%d" xOrigin="center" yOrigin="center" layer="4"/>'''
            % (cx, cy, w, h, cx, cy, w, h))

def forms_page(v):
    f = FORMS[v]
    x0, y0, x1, y1 = f['panel']
    return '''%s

<!-- Aeon Nox: Settings, Manage, Sources and the other screens, drawn over the page behind.
     Image paths are relative to this page's folder. -->

    <!-- Background -->
    <image src="images/background.png" x="0" y="0" width="%d" height="%d" layer="0"/>

%s

    <!-- Panel: the theme's info panel and its frame, stretched (seen at x %d-%d, y %d-%d) -->
%s

    <form x="%d" y="%d" width="%d" height="%d" xOrigin="center" yOrigin="center" labelWidth="%d"
          %s layer="3"/>

</layout>
''' % (head(f), f['W'], f['H'], topbar(f, 'RETROFE'), x0, x1, y0, y1, panel_images(x0, y0, x1, y1),
       (x0 + x1) // 2, (y0 + y1) // 2, f['form_w'], y1 - y0 - 40, f['label'], form_attrs('../'))

def package_page(v):
    f = FORMS[v]
    x0, y0, x1, y1 = f['pkg_panel']
    return '''%s

<!-- Aeon Nox: a package from the Catalog. The form takes the top of the screen, so the
     Catalog page behind it keeps showing the package's logo, kind, source and version in
     its info panel. (A form page has no selected item of its own, so it can't show the
     package's artwork itself.) -->

    <!-- Cover the Catalog's top part (the background squashed to fit) -->
    <image src="images/background.png" x="0" y="0" width="%d" height="%d" layer="0"/>

%s

    <!-- Panel (seen at x %d-%d, y %d-%d) -->
%s

    <form x="%d" y="%d" width="%d" height="%d" xOrigin="center" yOrigin="top" labelWidth="%d"
          %s layer="3"/>

</layout>
''' % (head(f), f['W'], f['pkg_cover'], topbar(f, 'CATALOG'), x0, x1, y0, y1, panel_images(x0, y0, x1, y1),
       (x0 + x1) // 2, y0 + 18, f['pkg_form_w'], y1 - y0 - 30, f['pkg_label'], form_attrs('../../'))

def keyboard_page(v):
    f = FORMS[v]
    w, h, bottom = f['kb']
    fw, fh = panel_size(w + 16, h + 16)
    frame_bottom = bottom + 8 + round(12 / 214 * fh)
    return '''%s

<!-- Aeon Nox: the on-screen keyboard, drawn over the screen being edited -->

    <!-- Frame -->
    <image src="images/system_info_overlay.png" x="center" y="%d" width="%d" height="%d" xOrigin="center" yOrigin="bottom" layer="2"/>

    <keyboard x="center" y="%d" width="%d" height="%d" xOrigin="center" yOrigin="bottom"
              font="fonts/OpenSans.ttf" loadFontSize="44" fontSize="38" fontColor="dedede" focusColor="ffffff"
              keyColor="%s" keyAlpha="0.09" focusKeyColor="%s" focusKeyAlpha="0.55"
              backgroundColor="070d12" backgroundAlpha="1" noteAlpha="0.65" layer="1"/>

</layout>
''' % (head(f), frame_bottom, fw, fh, bottom, w, h, ACCENT, ACCENT)

# ---------------------------------------------------------------------------------------

def splash():
    # Upstream's splash.xml points at a font the theme doesn't have and nests its
    # commented-out examples (fine for rapidxml, not XML); the video is unchanged.
    return '''<layout width="1920" height="1080" loadFontSize="32" fontColor="dedede">

<!-- Aeon Nox: the intro video, shown while RetroFE starts -->

    <video src="video/intro.mp4" x="0" y="0" width="1920" height="1080" alpha="1">
        <onExit>
            <set duration=".3">
                <animate type="alpha"  to="0"/>
                <animate type="volume" to="0"/>
            </set>
        </onExit>
    </video>

</layout>
'''

for variant in GEO:
    f = GEO[variant]['file']
    write(f, games_page(variant))
    write('collections/_menu/layout/' + f, menu_page(variant))
    write('collections/Home/layout/' + f, home_page(variant))
    write('collections/Catalog/layout/' + f, catalog_page(variant))
    write('forms/' + f, forms_page(variant).replace('src="images/', 'src="../images/'))
    write('forms/package/' + f, package_page(variant).replace('src="images/', 'src="../../images/'))
    write('forms/keyboard/' + f, keyboard_page(variant).replace('src="images/', 'src="../../images/'))
write('splash.xml', splash())

TEXT = {
    'collections/Home/medium_artwork/tagline/Catalog.txt': 'Collections, artwork and layouts to install',
    'collections/Home/medium_artwork/tagline/Settings.txt': 'Display, menus and video',
    'collections/Home/medium_artwork/tagline/Manage.txt': 'Your games, systems, menus and themes',
    'collections/Home/medium_artwork/tagline/Browse.txt': 'Your collections and games',
    'collections/Home/medium_artwork/description/Catalog.txt':
        'Everything your sources offer: collections, artwork, layouts and more. Choose a package to see what it is and install it. '
        'Downloads carry on in the background while you look around.',
    'collections/Home/medium_artwork/description/Settings.txt':
        'Window or full screen, which layout to use, whether RetroFE starts here on Home, and video playback. '
        'The Settings key opens it from anywhere, too.',
    'collections/Home/medium_artwork/description/Manage.txt':
        'Games, menus, systems, videos, emulators and themes: add, change and hide what RetroFE shows and how it starts it. '
        'See what the Catalog has installed, and the sources it uses.',
    'collections/Home/medium_artwork/description/Browse.txt':
        'Your collections, as set up in Manage or installed from the Catalog. Back brings you here again.',
    'collections/Catalog/medium_artwork/tagline/Catalog Sources.txt': 'Where packages come from',
    'collections/Catalog/medium_artwork/description/Catalog Sources.txt':
        'Add or remove sources: an index file on the web or on disk, or a folder of package archives on a USB stick or a NAS. '
        'You can check them all for updates here too.',
}
for rel, text in TEXT.items():
    write(rel, text)
