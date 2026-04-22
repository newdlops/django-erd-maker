from django.apps import AppConfig


class BrokenConfig(AppConfig):
    default_auto_field = "django.db.models.BigAutoField"
    name = "broken"
