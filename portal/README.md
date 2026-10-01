to avoid dbus as much as humanly viable, we actually just make our portals implementation a thin implementation over varlink interfaces.
weird? maybe. but even aside from dbus's fundemental flaws, i don't like writing code for it, and i don't like asking AI to do it for me.

you'll notice the dbus here is done by AI. the meta here is using AI to not have to use AI. we are playing 4D chess. bitch.

## implemented interfaces:

- Account portal
- Email portal
- File chooser portal
- Inhibit portal
- Notification portal
- Print portal
- Settings portal

Secrets portal is handled by kwalletd6, everything else falls back to gtk
