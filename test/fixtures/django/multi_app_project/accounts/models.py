from django.db import models


class Author(models.Model):
    email = models.EmailField(unique=True)


class Profile(models.Model):
    author = models.OneToOneField("accounts.Author", on_delete=models.CASCADE)
    bio = models.TextField(blank=True)
