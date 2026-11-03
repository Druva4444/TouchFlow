
const { St, Clutter, Gio, GLib } = imports.gi;
const Main = imports.ui.main;

class TouchFlowOSD {
    constructor() {
        this.hideTimeout = 0;
        this.signalId = 0;
        this.isShown = false;
        this.currentType = '';
        this.currentValue = -1;
        this.destroyed = false;

        this.osd = new St.BoxLayout({
            vertical: true,
            width: 340,
            height: 112,
            reactive: false,
            opacity: 0,
            scale_x: 0.90,
            scale_y: 0.90,
            translation_y: 16,
            visible: false,
            style: `
                background-color: rgba(22, 24, 32, 0.96);
                border: 1px solid rgba(255,255,255,0.13);
                border-radius: 22px;
                padding: 15px 20px;
                spacing: 13px;
            `
        });

        this.osd.set_pivot_point(0.5, 0.5);

        // HEADER
        this.header = new St.BoxLayout({
            vertical: false,
            height: 46,
            style: 'spacing: 13px;'
        });

        // ICON CONTAINER
        this.iconBox = new St.Widget({
            width: 46,
            height: 46,
            style: `
                background-color: rgba(138,180,248,0.15);
                border-radius: 15px;
            `
        });

        this.icon = new St.Icon({
            icon_name: 'audio-volume-high-symbolic',
            icon_size: 25,
            style: 'color: #8ab4f8;'
        });

        this.icon.set_position(10, 10);
        this.iconBox.add_child(this.icon);

        // TEXT
        this.textContainer = new St.BoxLayout({
            vertical: true,
            x_expand: true,
            y_align: Clutter.ActorAlign.CENTER,
            style: 'spacing: 3px;'
        });

        this.title = new St.Label({
            text: 'Volume',
            style: `
                color: #ffffff;
                font-size: 15px;
                font-weight: bold;
            `
        });

        this.subtitle = new St.Label({
            text: 'SYSTEM CONTROL',
            style: `
                color: rgba(255,255,255,0.48);
                font-size: 9px;
                font-weight: bold;
            `
        });

        this.textContainer.add_child(this.title);
        this.textContainer.add_child(this.subtitle);

        // PERCENTAGE
        this.percentage = new St.Label({
            text: '50%',
            y_align: Clutter.ActorAlign.CENTER,
            style: `
                color: #8ab4f8;
                font-size: 22px;
                font-weight: bold;
            `
        });

        this.header.add_child(this.iconBox);
        this.header.add_child(this.textContainer);
        this.header.add_child(this.percentage);

        // PROGRESS TRACK
        this.track = new St.Widget({
            width: 298,
            height: 8,
            style: `
                background-color: rgba(255,255,255,0.13);
                border-radius: 8px;
            `
        });

        this.fill = new St.Widget({
            width: 149,
            height: 8,
            style: `
                background-color: #8ab4f8;
                border-radius: 8px;
            `
        });

        this.track.add_child(this.fill);

        this.osd.add_child(this.header);
        this.osd.add_child(this.track);

        // ADD TO GNOME SHELL
        Main.layoutManager.addChrome(this.osd, {
            affectsStruts: false,
            trackFullscreen: true
        });

        this.positionOSD();

        // DBUS LISTENER
        this.signalId = Gio.DBus.session.signal_subscribe(
            null,
            'org.touchflow.OSD',
            'Update',
            '/org/touchflow/OSD',
            null,
            Gio.DBusSignalFlags.NONE,
            (connection, sender, path, iface, signal, params) => {
                if (this.destroyed)
                    return;

                try {
                    const [type, value] = params.deep_unpack();
                    this.update(type, value);
                } catch (e) {
                    logError(e, 'TouchFlow OSD signal error');
                }
            }
        );
    }

    // POSITION
    positionOSD() {
        const monitor = Main.layoutManager.primaryMonitor;

        if (!monitor)
            return;

        const width = 340;

        this.osd.set_position(
            monitor.x + Math.floor((monitor.width - width) / 2),
            monitor.y + monitor.height - 175
        );
    }

    // APPEARANCE
    configureAppearance(type, value) {
        let iconName;
        let accent;
        let background;

        if (type === 'volume') {
            if (value === 0)
                iconName = 'audio-volume-muted-symbolic';
            else if (value < 35)
                iconName = 'audio-volume-low-symbolic';
            else if (value < 70)
                iconName = 'audio-volume-medium-symbolic';
            else
                iconName = 'audio-volume-high-symbolic';

            accent = '#8ab4f8';
            background = 'rgba(138,180,248,0.15)';

            this.title.set_text('Volume');
        } else {
            iconName = 'display-brightness-symbolic';

            accent = '#ffd166';
            background = 'rgba(255,209,102,0.15)';

            this.title.set_text('Brightness');
        }

        this.icon.set_icon_name(iconName);
        this.icon.set_style(`color: ${accent};`);

        this.iconBox.set_style(`
            background-color: ${background};
            border-radius: 15px;
        `);

        this.fill.set_style(`
            background-color: ${accent};
            border-radius: 8px;
        `);

        this.percentage.set_style(`
            color: ${accent};
            font-size: 22px;
            font-weight: bold;
        `);
    }

    // ENTRANCE ANIMATION
    show() {
        this.positionOSD();

        if (this.isShown)
            return;

        this.isShown = true;

        this.osd.show();

        this.osd.ease({
            opacity: 255,
            scale_x: 1.0,
            scale_y: 1.0,
            translation_y: 0,
            duration: 420,
            mode: Clutter.AnimationMode.EASE_OUT_BACK
        });
    }

    // ICON PULSE
    animateIcon() {
        this.icon.remove_all_transitions();

        this.icon.scale_x = 1.0;
        this.icon.scale_y = 1.0;

        this.icon.ease({
            scale_x: 1.15,
            scale_y: 1.15,
            duration: 120,
            mode: Clutter.AnimationMode.EASE_OUT_QUAD
        });

        this.icon.ease({
            scale_x: 1.0,
            scale_y: 1.0,
            delay: 120,
            duration: 200,
            mode: Clutter.AnimationMode.EASE_OUT_BACK
        });
    }

    // MAIN UPDATE
    update(type, value) {
        if (this.destroyed)
            return;

        if (type !== 'volume' && type !== 'brightness')
            return;

        value = Math.max(0, Math.min(100, Number(value)));

        if (!Number.isFinite(value))
            return;

        this.configureAppearance(type, value);

        this.percentage.set_text(`${value}%`);

        // PROGRESS ANIMATION
        this.fill.ease({
            width: Math.round(298 * value / 100),
            duration: 300,
            mode: Clutter.AnimationMode.EASE_OUT_CUBIC
        });

        // ICON ANIMATION
        if (this.currentType !== type ||
            this.currentValue !== value) {
            this.animateIcon();
        }

        this.currentType = type;
        this.currentValue = value;

        // SHOW HUD
        this.show();

        // RESET HIDE TIMER
        if (this.hideTimeout) {
            GLib.source_remove(this.hideTimeout);
            this.hideTimeout = 0;
        }

        this.hideTimeout = GLib.timeout_add(
            GLib.PRIORITY_DEFAULT,
            1600,
            () => {
                this.hideTimeout = 0;
                this.hide();

                return GLib.SOURCE_REMOVE;
            }
        );
    }

    // EXIT ANIMATION
    hide() {
        if (!this.isShown)
            return;

        this.isShown = false;

        this.osd.ease({
            opacity: 0,
            scale_x: 0.94,
            scale_y: 0.94,
            translation_y: 14,
            duration: 320,
            mode: Clutter.AnimationMode.EASE_IN_QUAD,

            onComplete: () => {
                if (!this.isShown && !this.destroyed)
                    this.osd.hide();
            }
        });
    }

    // CLEANUP
    destroy() {
        this.destroyed = true;

        if (this.signalId) {
            Gio.DBus.session.signal_unsubscribe(this.signalId);
            this.signalId = 0;
        }

        if (this.hideTimeout) {
            GLib.source_remove(this.hideTimeout);
            this.hideTimeout = 0;
        }

        this.osd.remove_all_transitions();
        this.osd.destroy();
    }
}

let touchflow;

function init() {}

function enable() {
    touchflow = new TouchFlowOSD();
}

function disable() {
    if (touchflow) {
        touchflow.destroy();
        touchflow = null;
    }
}
